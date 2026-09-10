/*  This file is part of EmuFramework.

	Imagine is free software: you can redistribute it and/or modify
	it under the terms of the GNU General Public License as published by
	the Free Software Foundation, either version 3 of the License, or
	(at your option) any later version.

	Imagine is distributed in the hope that it will be useful,
	but WITHOUT ANY WARRANTY; without even the implied warranty of
	MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
	GNU General Public License for more details.

	You should have received a copy of the GNU General Public License
	along with EmuFramework.  If not, see <http://www.gnu.org/licenses/> */

#include <emuframework/SystemActionsView.hh>
#include <emuframework/EmuApp.hh>
#include <emuframework/EmuSystem.hh>
#include <emuframework/EmuVideo.hh>
#include <emuframework/EmuViewController.hh>
#include <emuframework/CreditsView.hh>
#include <emuframework/StateSlotView.hh>
#include <emuframework/BundledGamesView.hh>
#include <emuframework/Cheats.hh>
#include <emuframework/viewUtils.hh>
#include "InputOverridesView.hh"
#include "AutosaveSlotView.hh"
#include "ResetAlertView.hh"
#include <imagine/gui/TextEntry.hh>
#include <imagine/base/ApplicationContext.hh>
#include <imagine/util/format.hh>
#include <imagine/logger/logger.h>

namespace EmuEx
{

constexpr SystemLogger log{"SystemActionsView"};

static auto autoSaveName(EmuApp &app)
{
	return std::format("自动存档 ({})", app.autosaveManager.slotFullName());
}

static std::string saveAutosaveName(EmuApp &app)
{
	auto &autosaveManager = app.autosaveManager;
	if(!autosaveManager.timerFrequency().count())
		return "自动保存存档";
	return std::format("自动保存存档 (时间 {:%M:%S})",
		duration_cast<Seconds>(autosaveManager.saveTimer.nextFireDuration()));
}

SystemActionsView::SystemActionsView(ViewAttachParams attach, bool customMenu):
	TableView{"System Actions", attach, item},
	cheats
	{
		"Cheats", attach,
		[this](const Input::Event &e)
		{
			if(system().hasContent())
			{
				pushAndShow(makeView<CheatsView>(), e);
			}
		}
	},
	reset
	{
		"Reset", attach,
		[this](const Input::Event &e)
		{
			if(!system().hasContent())
				return;
			pushAndShowModal(resetAlertView(attachParams(), app()), e);
		}
	},
	autosaveSlot
	{
		autoSaveName(app()), attach,
		[this](const Input::Event &e) { pushAndShow(makeView<AutosaveSlotView>(), e); }
	},
	autosaveNow
	{
		saveAutosaveName(app()), attach,
		[this](TextMenuItem &item, const Input::Event &e)
		{
			if(!item.active())
				return;
			pushAndShowModal(makeView<YesNoAlertView>("是否载入存档?",
				YesNoAlertView::Delegates
				{
					.onYes = [this]
					{
						if(app().autosaveManager.save(AutosaveActionSource::Manual))
							app().showEmulation();
					}
				}), e);
		}
	},
	revertAutosave
	{
		"加载自动存档", attach,
		[this](TextMenuItem &item, const Input::Event &e)
		{
			if(!item.active())
				return;
			auto saveTime = app().autosaveManager.stateTimeAsString();
			if(saveTime.empty())
			{
				app().postMessage("没有已存档案");
				return;
			}
			pushAndShowModal(makeView<YesNoAlertView>(std::format("是否载入以下存档: {}?", saveTime),
				YesNoAlertView::Delegates
				{
					.onYes = [this]
					{
						if(app().autosaveManager.load(AutosaveActionSource::Manual))
							app().showEmulation();
					}
				}), e);
		}
	},
	stateSlot
	{
		"手动保存档案", attach,
		[this](const Input::Event &e)
		{
			pushAndShow(makeView<StateSlotView>(), e);
		}
	},
	inputOverrides
	{
		"输入覆盖", attach,
		[this](const Input::Event &e)
		{
			pushAndShow(makeView<InputOverridesView>(app().inputManager), e);
		}
	},
	addLauncherIcon
	{
		"添加游戏快捷方式到桌面", attach,
		[this](const Input::Event &e)
		{
			if(!system().hasContent())
				return;
			if(system().contentDirectory().empty())
			{
				// shortcuts to bundled games not yet supported
				return;
			}
			pushAndShowNewCollectValueInputView<const char*>(attachParams(), e, "快捷方式名称", system().contentDisplayName(),
				[this](CollectTextInputView &, auto str)
				{
					appContext().addLauncherIcon(str, system().contentLocation());
					app().postMessage(2, false, std::format("添加快捷方式:\n{}", str));
					return true;
				});
		}
	},
	screenshot
	{
		"截图下一帧", attach,
		[this](const Input::Event &e)
		{
			if(!system().hasContent())
				return;
			auto pathName = appContext().fileUriDisplayName(app().screenshotDirectory());
			if(pathName.empty())
			{
				app().postMessage("保存路径无效");
				return;
			}
			pushAndShowModal(makeView<YesNoAlertView>(std::format("将屏幕截图保存到文件夹 {}?", pathName),
				YesNoAlertView::Delegates
				{
					.onYes = [this]
					{
						app().video.takeGameScreenshot();
						system().runFrame({}, &app().video, nullptr);
					}
				}), e);
		}
	},
	resetSessionOptions
	{
		"重置已保存设置", attach,
		[this](const Input::Event &e)
		{
			if(!app().hasSavedSessionOptions())
				return;
			pushAndShowModal(makeView<YesNoAlertView>(
				"是否将当前运行的系统的已保存选项重置为默认值?某些选项仅在下次系统加载时生效.",
				YesNoAlertView::Delegates
				{
					.onYes = [this]
					{
						resetSessionOptions.setActive(false);
						app().deleteSessionOptions();
					}
				}), e);
		}
	},
	close
	{
		"关闭游戏", attach,
		[this](const Input::Event &e)
		{
			pushAndShowModal(app().makeCloseContentView(), e);
		}
	}
{
	if(!customMenu)
	{
		loadStandardItems();
	}
}

void SystemActionsView::onShow()
{
	if(app().viewController().isShowingEmulation())
		return;
	TableView::onShow();
	log.info("refreshing action menu state");
	assert(system().hasContent());
	autosaveSlot.compile(autoSaveName(app()));
	autosaveNow.compile(saveAutosaveName(app()));
	autosaveNow.setActive(app().autosaveManager.slotName() != noAutosaveName);
	revertAutosave.setActive(app().autosaveManager.slotName() != noAutosaveName);
	resetSessionOptions.setActive(app().hasSavedSessionOptions());
}

void SystemActionsView::loadStandardItems()
{
	if(EmuSystem::hasCheats)
	{
		item.emplace_back(&cheats);
	}
	item.emplace_back(&reset);
	item.emplace_back(&autosaveSlot);
	item.emplace_back(&revertAutosave);
	item.emplace_back(&autosaveNow);
	item.emplace_back(&stateSlot);
	item.emplace_back(&inputOverrides);
	if(used(addLauncherIcon))
		item.emplace_back(&addLauncherIcon);
	item.emplace_back(&screenshot);
	item.emplace_back(&resetSessionOptions);
	item.emplace_back(&close);
}

}
