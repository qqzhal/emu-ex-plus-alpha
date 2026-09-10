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

#include <emuframework/SystemOptionView.hh>
#include <emuframework/EmuApp.hh>
#include <emuframework/EmuOptions.hh>
#include <emuframework/viewUtils.hh>
#include "CPUAffinityView.hh"
#include <imagine/base/ApplicationContext.hh>
#include <imagine/gui/TextTableView.hh>
#include <imagine/fs/FS.hh>
#include <format>

namespace EmuEx
{

SystemOptionView::SystemOptionView(ViewAttachParams attach, bool customMenu):
	TableView{"系统设置", attach, item},
	autosaveTimerItem
	{
		{"关",    attach, {.id = 0}},
		{"5min",  attach, {.id = 5}},
		{"10分钟", attach, {.id = 10}},
		{"15分钟", attach, {.id = 15}},
		{"自定义", attach, [this](const Input::Event &e)
			{
				pushAndShowNewCollectValueRangeInputView<int, 0, maxAutosaveSaveFreq.count()>(attachParams(), e, "输入0到720之间的值", "",
					[this](CollectTextInputView &, auto val)
					{
						app().autosaveManager.saveTimer.frequency = Minutes{val};
						autosaveTimer.setSelected(MenuId{val}, *this);
						dismissPrevious();
						return true;
					});
				return false;
			}, {.id = defaultMenuId}
		},
	},
	autosaveTimer
	{
		"计时器", attach,
		MenuId{app().autosaveManager.saveTimer.frequency.count()},
		autosaveTimerItem,
		{
			.onSetDisplayString = [this](auto idx, Gfx::Text &t)
			{
				if(!idx)
					return false;
				t.resetString(std::format("{}", app().autosaveManager.saveTimer.frequency));
				return true;
			},
			.defaultItemOnSelect = [this](TextMenuItem &item) { app().autosaveManager.saveTimer.frequency = IG::Minutes{item.id}; }
		},
	},
	autosaveLaunchItem
	{
		{"主档案",            attach, {.id = AutosaveLaunchMode::Load}},
		{"主档案(无即时)", attach, {.id = AutosaveLaunchMode::LoadNoState}},
		{"无档案",         attach, {.id = AutosaveLaunchMode::NoSave}},
		{"选择档案",          attach, {.id = AutosaveLaunchMode::Ask}},
	},
	autosaveLaunch
	{
		"启动模式", attach,
		MenuId{app().autosaveManager.autosaveLaunchMode},
		autosaveLaunchItem,
		{
			.defaultItemOnSelect = [this](TextMenuItem &item) { app().autosaveManager.autosaveLaunchMode = AutosaveLaunchMode(item.id.val); }
		},
	},
	autosaveContent
	{
		"Content", attach,
		app().autosaveManager.saveOnlyBackupMemory,
		"即时&备份RAM", "仅备份RAM",
		[this](BoolMenuItem &item)
		{
			app().autosaveManager.saveOnlyBackupMemory = item.flipBoolValue(*this);
		}
	},
	confirmOverwriteState
	{
		"覆盖存档时提示", attach,
		app().confirmOverwriteState,
		[this](BoolMenuItem &item)
		{
			app().confirmOverwriteState = item.flipBoolValue(*this);
		}
	},
	fastModeSpeedItem
	{
		{"1.5x",  attach, {.id = 150}},
		{"2x",    attach, {.id = 200}},
		{"4x",    attach, {.id = 400}},
		{"8x",    attach, {.id = 800}},
		{"16x",   attach, {.id = 1600}},
		{"自定义", attach,
			[this](const Input::Event &e)
			{
				pushAndShowNewCollectValueRangeInputView<float, 1, 20>(attachParams(), e, "输入高于1.0到20.0之间的值", "",
					[this](CollectTextInputView &, auto val)
					{
						auto valAsInt = std::round(val * 100.f);
						app().setAltSpeed(AltSpeedMode::fast, valAsInt);
						fastModeSpeed.setSelected(MenuId{valAsInt}, *this);
						dismissPrevious();
						return true;
					});
				return false;
			}, {.id = defaultMenuId}
		},
	},
	fastModeSpeed
	{
		"快动作速度", attach,
		MenuId{app().altSpeed(AltSpeedMode::fast)},
		fastModeSpeedItem,
		{
			.onSetDisplayString = [this](auto, Gfx::Text& t)
			{
				t.resetString(std::format("{:g}x", app().altSpeedAsDouble(AltSpeedMode::fast)));
				return true;
			},
			.defaultItemOnSelect = [this](TextMenuItem &item) { app().setAltSpeed(AltSpeedMode::fast, item.id); }
		},
	},
	slowModeSpeedItem
	{
		{"0.25x", attach, {.id = 25}},
		{"0.50x", attach, {.id = 50}},
		{"自定义", attach,
			[this](const Input::Event &e)
			{
				pushAndShowNewCollectValueInputView<float>(attachParams(), e, "输入0.05以上到1.0之间的值", "",
					[this](CollectTextInputView &, auto val)
					{
						auto valAsInt = std::round(val * 100.f);
						if(app().setAltSpeed(AltSpeedMode::slow, valAsInt))
						{
							slowModeSpeed.setSelected(MenuId{valAsInt}, *this);
							dismissPrevious();
							return true;
						}
						else
						{
							app().postErrorMessage("值不在范围内");
							return false;
						}
					});
				return false;
			}, {.id = defaultMenuId}
		},
	},
	slowModeSpeed
	{
		"慢动作速度", attach,
		MenuId{app().altSpeed(AltSpeedMode::slow)},
		slowModeSpeedItem,
		{
			.onSetDisplayString = [this](auto, Gfx::Text& t)
			{
				t.resetString(std::format("{:g}x", app().altSpeedAsDouble(AltSpeedMode::slow)));
				return true;
			},
			.defaultItemOnSelect = [this](TextMenuItem &item) { app().setAltSpeed(AltSpeedMode::slow, item.id); }
		},
	},
	rewindStatesItem
	{
		{"0",  attach, {.id = 0}},
		{"30", attach, {.id = 30}},
		{"60", attach, {.id = 60}},
		{"自定义", attach, [this](const Input::Event &e)
			{
				pushAndShowNewCollectValueRangeInputView<int, 0, 50000>(attachParams(), e,
					"输入0到50000之间的值", std::to_string(app().rewindManager.maxStates),
					[this](CollectTextInputView &, auto val)
					{
						app().rewindManager.updateMaxStates(val);
						rewindStates.setSelected(val, *this);
						dismissPrevious();
						return true;
					});
				return false;
			}, {.id = defaultMenuId}
		},
	},
	rewindStates
	{
		"States", attach,
		MenuId{app().rewindManager.maxStates},
		rewindStatesItem,
		{
			.onSetDisplayString = [this](auto, Gfx::Text& t)
			{
				t.resetString(std::format("{}", app().rewindManager.maxStates));
				return true;
			},
			.defaultItemOnSelect = [this](TextMenuItem &item)
			{
				app().rewindManager.updateMaxStates(item.id);
				app().defaultVController().updateEnabledUIButtons();
			}
		},
	},
	rewindTimeInterval
	{
		"状态间隔(秒)", std::to_string(app().rewindManager.saveTimer.frequency.count()), attach,
		[this](const Input::Event &e)
		{
			pushAndShowNewCollectValueRangeInputView<int, 1, 60>(attachParams(), e,
				"输入1到60之间的值", std::to_string(app().rewindManager.saveTimer.frequency.count()),
				[this](CollectTextInputView &, auto val)
				{
					app().rewindManager.saveTimer.frequency = Seconds{val};
					rewindTimeInterval.set2ndName(std::to_string(val));
					return true;
				});
		}
	},
	performanceMode
	{
		"性能模式", attach,
		app().useSustainedPerformanceMode,
		"正常", "稳帧",
		[this](BoolMenuItem &item)
		{
			app().useSustainedPerformanceMode = item.flipBoolValue(*this);
		}
	},
	noopThread
	{
		"No-op Thread (Experimental)", attach,
		(bool)app().useNoopThread,
		[this](BoolMenuItem &item)
		{
			app().useNoopThread = item.flipBoolValue(*this);
		}
	},
	cpuAffinity
	{
		"配置CPU相关性", attach,
		[this](const Input::Event &e)
		{
			pushAndShow(makeView<CPUAffinityView>(appContext().cpuCount()), e);
		}
	},
	autosaveHeading{"自动保存设置", attach},
	rewindHeading{"回放设置", attach},
	otherHeading{"其他设置", attach}
{
	if(!customMenu)
	{
		loadStockItems();
	}
}

void SystemOptionView::loadStockItems()
{
	item.emplace_back(&autosaveHeading);
	item.emplace_back(&autosaveLaunch);
	item.emplace_back(&autosaveTimer);
	item.emplace_back(&autosaveContent);
	item.emplace_back(&rewindHeading);
	item.emplace_back(&rewindStates);
	item.emplace_back(&rewindTimeInterval);
	item.emplace_back(&otherHeading);
	item.emplace_back(&confirmOverwriteState);
	item.emplace_back(&fastModeSpeed);
	item.emplace_back(&slowModeSpeed);
	if(used(performanceMode) && appContext().hasSustainedPerformanceMode())
		item.emplace_back(&performanceMode);
	if(used(noopThread))
		item.emplace_back(&noopThread);
	if(used(cpuAffinity) && appContext().cpuCount() > 1)
		item.emplace_back(&cpuAffinity);
}

}
