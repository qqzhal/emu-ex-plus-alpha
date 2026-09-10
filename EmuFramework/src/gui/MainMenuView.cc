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

#include <emuframework/MainMenuView.hh>
#include <emuframework/EmuApp.hh>
#include <emuframework/EmuSystem.hh>
#include <emuframework/CreditsView.hh>
#include <emuframework/FilePicker.hh>
#include <emuframework/VideoOptionView.hh>
#include <emuframework/TouchConfigView.hh>
#include <emuframework/BundledGamesView.hh>
#include "InputManagerView.hh"
#include "RecentContentView.hh"
#include "FrameTimingView.hh"
#include <emuframework/EmuOptions.hh>
#include <imagine/gui/AlertView.hh>
#include <imagine/base/ApplicationContext.hh>
#include <imagine/fs/FS.hh>
#include <imagine/bluetooth/BluetoothInputDevice.hh>
#include <format>
#include <imagine/logger/logger.h>

namespace EmuEx
{

constexpr SystemLogger log{"AppMenus"};

class OptionCategoryView : public TableView, public EmuAppHelper
{
public:
	OptionCategoryView(ViewAttachParams attach);

protected:
	TextMenuItem subConfig[8];
};

static void onScanStatus(EmuApp &app, BluetoothScanState status, int arg);

template <class ViewT>
static void handledFailedBTAdapterInit(ViewT& view, [[maybe_unused]] ViewAttachParams attach, [[maybe_unused]] const Input::Event& e)
{
	view.app().postErrorMessage("无法初始化蓝牙适配器");
	#ifdef CONFIG_BLUETOOTH_BTSTACK
	if(!FS::exists("/var/lib/dpkg/info/ch.ringwald.btstack.list"))
	{
		view.pushAndShowModal(std::make_unique<YesNoAlertView>(attach, "BTstack not found, open Cydia and install?",
			YesNoAlertView::Delegates
			{
				.onYes = [](View &v){ v.appContext().openURL("cydia://package/ch.ringwald.btstack"); }
			}), e, false);
	}
	#endif
}

MainMenuView::MainMenuView(ViewAttachParams attach, bool customMenu):
	TableView{EmuApp::mainViewName(), attach, item},
	loadGame
	{
		"打开游戏", attach,
		[this](const Input::Event &e)
		{
			pushAndShow(FilePicker::forLoading(attachParams(), e), e, false);
		}
	},
	systemActions
	{
		"System Actions", attach,
		[this](const Input::Event &e)
		{
			if(!system().hasContent())
				return;
			pushAndShow(app().makeView(attachParams(), EmuApp::ViewID::SYSTEM_ACTIONS), e);
		}
	},
	recentGames
	{
		"最近游戏", attach,
		[this](const Input::Event &e)
		{
			if(app().recentContent.size())
			{
				pushAndShow(makeView<RecentContentView>(app().recentContent), e);
			}
		}
	},
	bundledGames
	{
		"Bundled Content", attach,
		[this](const Input::Event &e)
		{
			pushAndShow(makeView<BundledGamesView>(), e);
		}
	},
	options
	{
		"设置", attach,
		[this](const Input::Event &e)
		{
			pushAndShow(makeView<OptionCategoryView>(), e);
		}
	},
	onScreenInputManager
	{
		"虚拟键盘设置", attach,
		[this](const Input::Event &e)
		{
			pushAndShow(makeView<TouchConfigView>(app().defaultVController()), e);
		}
	},
	inputManager
	{
		"外接手柄设置", attach,
		[this](const Input::Event &e)
		{
			pushAndShow(makeView<InputManagerView>(app().inputManager), e);
		}
	},
	benchmark
	{
		"游戏测帧", attach,
		[this](const Input::Event &e)
		{
			pushAndShow(FilePicker::forBenchmarking(attachParams(), e), e, false);
		}
	},
	scanWiimotes
	{
		"扫描Wiimotes/ICP/JS1", attach,
		[this](const Input::Event &e)
		{
			app().bluetoothAdapter.openDefault();
			if(app().bluetoothAdapter.isOpen())
			{
				if(Bluetooth::scanForDevices(appContext(), app().bluetoothAdapter,
					[this](BluetoothAdapter &, BluetoothScanState status, int arg)
					{
						onScanStatus(app(), status, arg);
					}))
				{
					app().postMessage(4, false, "开始扫描中...\n(有关特定于设备的帮助,请参见网站)");
				}
				else
				{
					app().postMessage(1, false, "持续扫描中");
				}
			}
			else
			{
				handledFailedBTAdapterInit(*this, attachParams(), e);
			}
			postDraw();
		}
	},
	bluetoothDisconnect
	{
		"断开蓝牙连接", attach,
		[this](const Input::Event &e)
		{
			auto devConnected = Bluetooth::devsConnected(appContext());
			if(devConnected)
			{
				pushAndShowModal(makeView<YesNoAlertView>(std::format("是否断开 {} 蓝牙设备?", devConnected),
					YesNoAlertView::Delegates{.onYes = [this]{ app().closeBluetoothConnections(); }}), e);
			}
		}
	},
	acceptPS3ControllerConnection
	{
		"Scan for PS3 Controller", attach,
		[this](const Input::Event &e)
		{
			app().bluetoothAdapter.openDefault();
			if(app().bluetoothAdapter.isOpen())
			{
				app().postMessage(4, "Prepare to push the PS button");
				auto startedScan = Bluetooth::listenForDevices(appContext(), app().bluetoothAdapter,
					[this](BluetoothAdapter&, BluetoothScanState status, int arg)
					{
						switch(status)
						{
							case BluetoothScanState::InitFailed:
							{
								app().postErrorMessage(Config::envIsLinux ? 8 : 2,
									Config::envIsLinux ?
										"Unable to register server, make sure this executable has cap_net_bind_service enabled and bluetoothd isn't running" :
										"Bluetooth setup failed");
								break;
							}
							case BluetoothScanState::Complete:
							{
								app().postMessage(4, "Push the PS button on your controller\n(see website for pairing help)");
								break;
							}
							default: onScanStatus(app(), status, arg);
						}
					});
				if(!startedScan)
				{
					app().postMessage(1, "持续扫描中");
				}
			}
			else
			{
				handledFailedBTAdapterInit(*this, attachParams(), e);
			}
			postDraw();
		}
	},
	about
	{
		"关于", attach,
		[this](const Input::Event &e)
		{
			pushAndShow(makeView<CreditsView>(EmuSystem::creditsViewStr), e);
		}
	},
	exitApp
	{
		"退出", attach,
		[this]()
		{
			appContext().exit();
		}
	}
{
	if(!customMenu)
	{
		reloadItems();
	}
}

static void onScanStatus(EmuApp &app, BluetoothScanState status, int arg)
{
	switch(status)
	{
		case BluetoothScanState::InitFailed:
		{
			if(Config::envIsIOS)
			{
				app.postErrorMessage("BTstack power on failed, make sure the iOS Bluetooth stack is not active");
			}
			break;
		}
		case BluetoothScanState::Failed:
		{
			app.postErrorMessage("扫描失败");
			break;
		}
		case BluetoothScanState::NoDevs:
		{
			app.postMessage("未找到设备");
			break;
		}
		case BluetoothScanState::Processing:
		{
			app.postMessage(2, 0, std::format("检测到 {} 设备...", arg));
			break;
		}
		case BluetoothScanState::NameFailed:
		{
			app.postErrorMessage("读取设备名称失败");
			break;
		}
		case BluetoothScanState::Complete:
		{
			int devs = Bluetooth::pendingDevs();
			if(devs)
			{
				app.postMessage(2, 0, std::format("正在连接 {} 设备...", devs));
				Bluetooth::connectPendingDevs(app.bluetoothAdapter);
			}
			else
			{
				app.postMessage("扫描完成,没有可识别设备");
			}
			break;
		}
		case BluetoothScanState::Cancelled: break;
		/*case BluetoothScanState::SocketOpenFailed:
		{
			app.postErrorMessage("Failed opening a Bluetooth connection");
		}*/
	}
};

void MainMenuView::onShow()
{
	TableView::onShow();
	log.info("refreshing main menu state");
	recentGames.setActive(app().recentContent.size());
	systemActions.setActive(system().hasContent());
	bluetoothDisconnect.setActive(Bluetooth::devsConnected(appContext()));
}

void MainMenuView::loadFileBrowserItems()
{
	item.emplace_back(&loadGame);
	item.emplace_back(&recentGames);
	if(EmuSystem::hasBundledGames && app().showsBundledGames)
	{
		item.emplace_back(&bundledGames);
	}
}

void MainMenuView::loadStandardItems()
{
	item.emplace_back(&systemActions);
	item.emplace_back(&onScreenInputManager);
	item.emplace_back(&inputManager);
	item.emplace_back(&options);
	if(used(scanWiimotes) && app().showsBluetoothScan)
	{
		item.emplace_back(&scanWiimotes);
		if(used(acceptPS3ControllerConnection))
			item.emplace_back(&acceptPS3ControllerConnection);
		item.emplace_back(&bluetoothDisconnect);
	}
	item.emplace_back(&benchmark);
	item.emplace_back(&about);
	item.emplace_back(&exitApp);
}

void MainMenuView::reloadItems()
{
	item.clear();
	loadFileBrowserItems();
	loadStandardItems();
}

OptionCategoryView::OptionCategoryView(ViewAttachParams attach):
	TableView
	{
		"设置",
		attach,
		[this](ItemMessage msg) -> ItemReply
		{
			return msg.visit(overloaded
			{
				[&](const ItemsMessage&) -> ItemReply { return EmuApp::hasGooglePlayStoreFeatures() ? std::size(subConfig) : std::size(subConfig)-1; },
				[&](const GetItemMessage& m) -> ItemReply { return &subConfig[m.idx]; },
			});
		}
	},
	subConfig
	{
		{
			"帧时序", attach,
			[this](const Input::Event &e)
			{
				pushAndShow(makeView<FrameTimingView>(), e);
			}
		},
		{
			"视频", attach,
			[this](const Input::Event &e)
			{
				pushAndShow(app().makeView(attachParams(), EmuApp::ViewID::VIDEO_OPTIONS), e);
			}
		},
		{
			"音频", attach,
			[this](const Input::Event &e)
			{
				pushAndShow(app().makeView(attachParams(), EmuApp::ViewID::AUDIO_OPTIONS), e);
			}
		},
		{
			"系统", attach,
			[this](const Input::Event &e)
			{
				pushAndShow(app().makeView(attachParams(), EmuApp::ViewID::SYSTEM_OPTIONS), e);
			}
		},
		{
			"文件路径", attach,
			[this](const Input::Event &e)
			{
				pushAndShow(app().makeView(attachParams(), EmuApp::ViewID::FILE_PATH_OPTIONS), e);
			}
		},
		{
			"界面", attach,
			[this](const Input::Event &e)
			{
				pushAndShow(app().makeView(attachParams(), EmuApp::ViewID::GUI_OPTIONS), e);
			}
		},
		{
			"在线更新", attach,
			[this]
			{
				appContext().openURL("https://www.explusalpha.com/contents/emuex/documentation");
			}
		}
	}
{
	if(EmuApp::hasGooglePlayStoreFeatures())
	{
		subConfig[lastIndex(subConfig)] =
		{
			"Beta Testing Opt-in/out", attach,
			[this]()
			{
				appContext().openURL(std::format("https://play.google.com/apps/testing/{}", appContext().applicationId));
			}
		};
	}
}

}
