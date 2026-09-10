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

#include "FrameTimingView.hh"
#include <emuframework/EmuApp.hh>
#include <emuframework/EmuAppHelper.hh>
#include <emuframework/EmuViewController.hh>
#include <emuframework/viewUtils.hh>
#include <imagine/base/Screen.hh>
#include <imagine/base/ApplicationContext.hh>
#include <imagine/gfx/Renderer.hh>
#include <imagine/gfx/RendererCommands.hh>
#include <format>

namespace EmuEx
{

static std::string makeFrameRateStr(VideoSystem vidSys, const OutputTimingManager& mgr)
{
	auto opt = mgr.frameRateOption(vidSys);
	if(opt == OutputTimingManager::autoOption)
		return "自动";
	else if(opt == OutputTimingManager::originalOption)
		return "Original";
	else
		return std::format("{:g}Hz", toHz(opt));
}

FrameTimingView::FrameTimingView(ViewAttachParams attach):
	TableView{"帧时序设置", attach, item},
	frameIntervalItem
	{
		{"完整（无跳帧）", attach, {.id = 0}},
		{"满帧",           attach, {.id = 1}},
		{"1/2",            attach, {.id = 2}},
		{"1/3",            attach, {.id = 3}},
		{"1/4",            attach, {.id = 4}},
	},
	frameInterval
	{
		"目标帧率", attach,
		MenuId{app().frameInterval},
		frameIntervalItem,
		MultiChoiceMenuItem::Config
		{
			.defaultItemOnSelect = [this](TextMenuItem &item) { app().frameInterval.setUnchecked(item.id); }
		},
	},
	frameRateItems
	{
		{"自动 (帧率相似时匹配屏幕)", attach,
			[this]
			{
				onFrameRateChange(activeVideoSystem, OutputTimingManager::autoOption);
			}, {.id = OutputTimingManager::autoOption.count()}
		},
		{"原始 (使用模拟器默认帧率)", attach,
			[this]
			{
				onFrameRateChange(activeVideoSystem, OutputTimingManager::originalOption);
			}, {.id = OutputTimingManager::originalOption.count()}
		},
		{"自定义帧率", attach,
			[this](const Input::Event &e)
			{
				pushAndShowNewCollectValueInputView<std::pair<double, double>>(attachParams(), e,
					"输入小数或分数", "",
					[this](CollectTextInputView&, auto val)
					{
						if(onFrameRateChange(activeVideoSystem, fromSeconds<SteadyClockDuration>(val.second / val.first)))
						{
							if(activeVideoSystem == VideoSystem::NATIVE_NTSC)
								frameRate.setSelected(defaultMenuId, *this);
							else
								frameRatePAL.setSelected(defaultMenuId, *this);
							dismissPrevious();
							return true;
						}
						else
							return false;
					});
				return false;
			}, {.id = defaultMenuId}
		},
	},
	frameRate
	{
		"输入帧率", attach,
		app().outputTimingManager.frameRateOptionAsMenuId(VideoSystem::NATIVE_NTSC),
		frameRateItems,
		{
			.onSetDisplayString = [this](auto, Gfx::Text& t)
			{
				t.resetString(makeFrameRateStr(VideoSystem::NATIVE_NTSC, app().outputTimingManager));
				return true;
			},
			.onSelect = [this](MultiChoiceMenuItem &item, View &view, const Input::Event &e)
			{
				activeVideoSystem = VideoSystem::NATIVE_NTSC;
				item.defaultOnSelect(view, e);
			},
		},
	},
	frameRatePAL
	{
		"输入帧率(PAL)", attach,
		app().outputTimingManager.frameRateOptionAsMenuId(VideoSystem::PAL),
		frameRateItems,
		{
			.onSetDisplayString = [this](auto, Gfx::Text& t)
			{
				t.resetString(makeFrameRateStr(VideoSystem::PAL, app().outputTimingManager));
				return true;
			},
			.onSelect = [this](MultiChoiceMenuItem &item, View &view, const Input::Event &e)
			{
				activeVideoSystem = VideoSystem::PAL;
				item.defaultOnSelect(view, e);
			},
		},
	},
	frameTimingStats
	{
		"显示帧计时统计", attach,
		app().showFrameTimingStats,
		[this](BoolMenuItem &item) { app().showFrameTimingStats = item.flipBoolValue(*this); }
	},
	lowLatencyVideo
	{
		"低延迟模式", attach,
		app().lowLatencyVideo,
		[this](BoolMenuItem& item) { app().setLowLatencyVideo(item.flipBoolValue(*this)); }
	},
	frameClockItems
	{
		[&]()
		{
			StaticArrayList<TextMenuItem, maxFrameClockItems> frameClockItems;
			frameClockItems.emplace_back("自动", attach, MenuItem::Config{.id = FrameClockSource::Unset});
			if(app().emuWindow().supportsFrameClockSource(FrameClockSource::Screen))
				frameClockItems.emplace_back("屏幕(用于标准显示器)", attach, MenuItem::Config{.id = FrameClockSource::Screen});
			if(app().emuWindow().supportsFrameClockSource(FrameClockSource::Renderer))
				frameClockItems.emplace_back("Renderer (For drivers with double buffering)", attach, MenuItem::Config{.id = FrameClockSource::Renderer});
			frameClockItems.emplace_back("定时器(用于可变刷新率显示器)", attach, MenuItem::Config{.id = FrameClockSource::Timer});
			return frameClockItems;
		}()
	},
	frameClock
	{
		"帧时钟", attach,
		MenuId{FrameClockSource(app().frameClockSource)},
		frameClockItems,
		MultiChoiceMenuItem::Config
		{
			.onSetDisplayString = [this](auto, Gfx::Text& t)
			{
				t.resetString(wise_enum::to_string(app().effectiveFrameClockSource()));
				return true;
			},
			.defaultItemOnSelect = [this](TextMenuItem &item)
			{
				app().frameClockSource = FrameClockSource(item.id.val);
				app().video.resetImage(); // texture can switch between single/double buffered
			}
		},
	},
	outputRateModeItems
	{
		{"自动",                                     attach, MenuItem::Config{.id = OutputFrameRateMode::Auto}},
		{"检测(在模拟期间计算帧率)", attach, MenuItem::Config{.id = OutputFrameRateMode::Detect}},
		{"屏幕(直接使用报告的帧率)",      attach, MenuItem::Config{.id = OutputFrameRateMode::Screen}},
	},
	outputRateMode
	{
		"输出帧率", attach,
		MenuId{OutputFrameRateMode(app().outputFrameRateMode)},
		outputRateModeItems,
		MultiChoiceMenuItem::Config
		{
			.onSetDisplayString = [this](auto, Gfx::Text& t)
			{
				t.resetString(wise_enum::to_string(app().effectiveOutputFrameRateMode()));
				return true;
			},
			.defaultItemOnSelect = [this](TextMenuItem &item)
			{
				app().outputFrameRateMode = OutputFrameRateMode(item.id.val);
			}
		},
	},
	presentModeItems
	{
		{"自动",                              attach, MenuItem::Config{.id = Gfx::PresentMode::Auto}},
		{"即时(用于可变刷新率/垂直同步关闭的情况)", attach, MenuItem::Config{.id = Gfx::PresentMode::Immediate}},
		{"队列(用于标准情况)",         attach, MenuItem::Config{.id = Gfx::PresentMode::FIFO}},
	},
	presentMode
	{
		"当前模式", attach,
		MenuId{Gfx::PresentMode(app().presentMode)},
		presentModeItems,
		MultiChoiceMenuItem::Config
		{
			.onSetDisplayString = [this](auto, Gfx::Text& t)
			{
				t.resetString(renderer().evalPresentMode(app().emuWindow(), app().presentMode) == Gfx::PresentMode::FIFO ? "排队" : "即时");
				return true;
			},
			.defaultItemOnSelect = [this](TextMenuItem &item)
			{
				app().presentMode = Gfx::PresentMode(item.id.val);
			}
		},
	},
	screenFrameRateItems
	{
		[&]
		{
			std::vector<TextMenuItem> items;
			items.emplace_back("关", attach, [this]() { app().overrideScreenFrameRate = 0; }, MenuItem::Config{.id = 0});
			for(auto rate : app().emuScreen().supportedFrameRates())
			{
				doIfUsed(screenFrameRateItems, [&]([[maybe_unused]] auto& _)
				{
					items.emplace_back(std::format("{:g}Hz", rate.hz()), attach,
					[this, hz = rate.hz()]()
					{
						app().overrideScreenFrameRate = hz;
					},
					MenuItem::Config{.id = std::bit_cast<MenuId>(float(rate.hz()))});
				});
			}
			return items;
		}()
	},
	screenFrameRate
	{
		"覆盖屏幕帧速率", attach,
		std::bit_cast<MenuId>(float(app().overrideScreenFrameRate)),
		screenFrameRateItems
	},
	blankFrameInsertion
	{
		"允许插入空白帧", attach,
		app().allowBlankFrameInsertion,
		[this](BoolMenuItem &item) { app().allowBlankFrameInsertion = item.flipBoolValue(*this); }
	},
	advancedHeading{"高级", attach}
{
	loadStockItems();
}

void FrameTimingView::loadStockItems()
{
	item.emplace_back(&frameInterval);
	item.emplace_back(&frameRate);
	if(EmuSystem::hasPALVideoSystem)
	{
		item.emplace_back(&frameRatePAL);
	}
	if(app().emuWindow().supportsFrameClockSource(FrameClockSource::Screen))
		item.emplace_back(&outputRateMode);
	item.emplace_back(&frameTimingStats);
	item.emplace_back(&advancedHeading);
	item.emplace_back(&frameClock);
	if(used(presentMode))
		item.emplace_back(&presentMode);
	item.emplace_back(&blankFrameInsertion);
	if(used(screenFrameRate) && app().emuScreen().supportedFrameRates().size() > 1)
		item.emplace_back(&screenFrameRate);
	item.emplace_back(&lowLatencyVideo);
}

bool FrameTimingView::onFrameRateChange(VideoSystem vidSys, SteadyClockDuration d)
{
	if(!app().outputTimingManager.setFrameRateOption(vidSys, d))
	{
		app().postMessage(4, true, std::format("{:g}Hz 不在有效范围内", toHz(d)));
		return false;
	}
	return true;
}

}
