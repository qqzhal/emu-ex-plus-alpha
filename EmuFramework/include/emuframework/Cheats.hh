#pragma once

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

#include <emuframework/EmuApp.hh>
#include <emuframework/EmuAppHelper.hh>
#include <emuframework/FilePicker.hh>
#include <emuframework/viewUtils.hh>
#include <imagine/gui/TableView.hh>
#include <imagine/gui/AlertView.hh>
#include <imagine/gui/MenuItem.hh>
#include <algorithm>
#include <cctype>
#include <functional>
#include <string>
#include <string_view>
#include <vector>

namespace EmuEx
{

using namespace IG;

// .cht 导入的互斥组命名约定: "组名 · 选项名"，秘籍列表按此前缀聚合为单选
inline constexpr std::string_view cheatGroupSep = " · ";

inline bool importChtFileFilter(std::string_view name)
{
	auto endsWithCI = [](std::string_view s, std::string_view suffix)
	{
		return s.size() >= suffix.size() && std::equal(suffix.rbegin(), suffix.rend(), s.rbegin(),
			[](char a, char b) { return tolower((unsigned char)a) == tolower((unsigned char)b); });
	};
	return endsWithCI(name, ".cht");
}

// 互斥组单选页: 列出组内全部变体，点选即启用该变体并关闭组内其它条目
class CheatGroupSelectView : public TableView, public EmuAppHelper
{
public:
	struct Entry
	{
		Cheat *c;
		std::string name;
	};
	CheatGroupSelectView(ViewAttachParams attach, std::string groupName_, std::vector<Entry> entries_,
		std::function<void()> onChanged_):
		TableView
		{
			groupName_,
			attach,
			[this](ItemMessage msg) -> ItemReply
			{
				return msg.visit(overloaded
				{
					[&](const ItemsMessage&) -> ItemReply { return items.size(); },
					[&](const GetItemMessage& m) -> ItemReply { return items[m.idx]; },
				});
			}
		},
		groupName{std::move(groupName_)},
		entries{std::move(entries_)},
		onChanged{std::move(onChanged_)}
	{
		loadItems();
	}

protected:
	std::string groupName;
	std::vector<Entry> entries;
	std::function<void()> onChanged;
	std::vector<TextMenuItem> item;
	std::vector<MenuItem*> items;

	std::string shortName(const std::string &name) const
	{
		auto prefix = groupName + std::string{cheatGroupSep};
		return name.starts_with(prefix) ? name.substr(prefix.size()) : name;
	}

	void loadItems()
	{
		item.clear();
		items.clear();
		item.reserve(entries.size() + 1);
		items.reserve(entries.size() + 1);
		item.emplace_back("不启用", attachParams(), [this](const Input::Event &)
		{
			for(auto &e : entries)
				system().setCheatEnabled(*e.c, false);
			changed();
		});
		items.emplace_back(&item.back());
		for(auto &e : entries)
		{
			auto label = std::string{system().isCheatEnabled(*e.c) ? "● " : "○ "} + shortName(e.name);
			item.emplace_back(std::move(label), attachParams(),
				[this, c = e.c](const Input::Event &)
				{
					for(auto &e2 : entries)
						system().setCheatEnabled(*e2.c, false);
					system().setCheatEnabled(*c, true);
					changed();
				});
			items.emplace_back(&item.back());
		}
	}

	void changed()
	{
		loadItems();
		place();
		postDraw();
		if(onChanged)
			onChanged();
	}
};

class CheatsView : public TableView, public EmuAppHelper
{
public:
	CheatsView(ViewAttachParams attach):
		TableView
		{
			"秘籍",
			attach,
			[this](ItemMessage msg) -> ItemReply
			{
				return msg.visit(overloaded
				{
					[&](const ItemsMessage&) -> ItemReply { return items.size(); },
					[&](const GetItemMessage& m) -> ItemReply { return items[m.idx]; },
				});
			}
		},
		import
		{
			"从 .cht 文件导入", attach,
			[this](const Input::Event &e)
			{
				auto fPicker = makeView<FilePicker>(FSPicker::Mode::FILE, &importChtFileFilter, e, false);
				// 记住上次导入的目录，下次打开直接定位
				if(app().chtPath.size())
					fPicker->setPath(app().chtPath, e);
				fPicker->setOnChangePath(
					[this](FSPicker &picker, const Input::Event &)
					{
						app().chtPath = std::string{picker.path()};
					});
				fPicker->setOnSelectPath(
					[this](FSPicker &picker, CStringView path, std::string_view, const Input::Event &)
					{
						app().chtPath = std::string{FS::dirname(path)};
						if(system().importCheatsFile(app(), path) >= 0)
						{
							onCheatsChanged();
							picker.dismiss();
						}
					});
				pushAndShowModal(std::move(fPicker), e);
			}
		},
		edit
		{
			"添加/编辑", attach,
			[this](const Input::Event &e)
			{
				auto editCheatsView = app().makeEditCheatsView(attachParams(), *this);
				pushAndShow(std::move(editCheatsView), e);
			}
		}
	{
		loadCheatItems();
	}

	void onCheatsChanged()
	{
		auto selectedCell = selected;
		loadCheatItems();
		highlightCell(selectedCell);
		place();
	}

protected:
	TextMenuItem import, edit;
	std::vector<BoolMenuItem> cheats;
	std::vector<DualTextMenuItem> groups;
	std::vector<MenuItem*> items;

	// .cht 导入的互斥组（名字含 "组名 · 选项名" 前缀）聚合为一行单选入口；
	// 单选项组与手动添加的条目显示为普通开关
	void loadCheatItems()
	{
		cheats.clear();
		groups.clear();
		items.clear();
		struct Entry { Cheat *c; std::string name; };
		std::vector<Entry> all;
		system().forEachCheat([&](Cheat &c, std::string_view name)
		{
			all.push_back({&c, std::string{name}});
			return true;
		});
		std::vector<std::pair<std::string, std::vector<Entry*>>> groupList;
		std::vector<Entry*> plain;
		for(auto &e : all)
		{
			auto sep = e.name.find(cheatGroupSep);
			if(sep == std::string::npos)
			{
				plain.push_back(&e);
				continue;
			}
			auto gName = e.name.substr(0, sep);
			auto found = std::find_if(groupList.begin(), groupList.end(),
				[&](auto &g) { return g.first == gName; });
			if(found == groupList.end())
				groupList.emplace_back(std::move(gName), std::vector<Entry*>{&e});
			else
				found->second.push_back(&e);
		}
		size_t singleItemGroups = 0;
		for(auto &g : groupList)
			singleItemGroups += g.second.size() == 1;
		cheats.reserve(plain.size() + singleItemGroups);
		groups.reserve(groupList.size());
		items.reserve(2 + plain.size() + singleItemGroups + groupList.size());
		items.emplace_back(&import);
		items.emplace_back(&edit);
		for(auto &g : groupList)
		{
			if(g.second.size() == 1)
			{
				auto *row = g.second[0];
				cheats.emplace_back(row->name, attachParams(), system().isCheatEnabled(*row->c),
					[this, c = row->c](BoolMenuItem &item)
					{
						system().setCheatEnabled(*c, item.flipBoolValue(*this));
					});
				items.emplace_back(&cheats.back());
			}
			else
			{
				Cheat *active = nullptr;
				for(auto *e : g.second)
					if(system().isCheatEnabled(*e->c))
					{
						active = e->c;
						break;
					}
				std::string current = "未启用";
				if(active)
				{
					auto fullName = system().cheatName(*active);
					auto sep = fullName.find(cheatGroupSep);
					current = (sep == std::string_view::npos) ? std::string{fullName}
						: std::string{fullName.substr(sep + cheatGroupSep.size())};
				}
				std::vector<CheatGroupSelectView::Entry> groupEntries;
				groupEntries.reserve(g.second.size());
				for(auto *e : g.second)
					groupEntries.push_back({e->c, e->name});
				groups.emplace_back(g.first, std::move(current), attachParams(),
					[this, groupEntries, groupName](const Input::Event &e)
					{
						pushAndShow(makeView<CheatGroupSelectView>(std::move(groupName),
							std::move(groupEntries), [this]{ onCheatsChanged(); }), e);
					});
				items.emplace_back(&groups.back());
			}
		}
		for(auto *e : plain)
		{
			cheats.emplace_back(e->name, attachParams(), system().isCheatEnabled(*e->c),
				[this, c = e->c](BoolMenuItem &item)
				{
					system().setCheatEnabled(*c, item.flipBoolValue(*this));
				});
			items.emplace_back(&cheats.back());
		}
	}
};

class BaseEditCheatsView : public TableView, public EmuAppHelper
{
public:
	BaseEditCheatsView(ViewAttachParams attach, CheatsView& cheatsView, TableView::ItemSourceDelegate itemSrc):
		TableView
		{
			"编辑秘籍",
			attach,
			itemSrc
		},
		cheatsViewPtr{&cheatsView}
	{
		loadCheatItems();
	}

	void onCheatsChanged()
	{
		auto selectedCell = selected;
		loadCheatItems();
		highlightCell(selectedCell);
		place();
		cheatsViewPtr->onCheatsChanged();
	}

protected:
	std::vector<TextMenuItem> cheats;
	CheatsView* cheatsViewPtr;

	void loadCheatItems()
	{
		cheats.clear();
		system().forEachCheat([this](Cheat& c, std::string_view name)
		{
			cheats.emplace_back(name, attachParams(), [this, &c](const Input::Event& e)
			{
				pushAndShow(app().makeEditCheatView(attachParams(), c, *this), e);
			});
			return true;
		});
	}

	void addNewCheat(const char* promptStr, const Input::Event& e, unsigned flags = 0)
	{
		pushAndShowNewCollectTextInputView(attachParams(), e, promptStr, "",
			[this, flags](CollectTextInputView& view, const char* str)
			{
				auto cheatPtr = system().newCheat(app(), "", {str, flags});
				if(!cheatPtr)
					return true;
				onCheatsChanged();
				view.dismiss();
				pushAndShowNewCollectTextInputView(attachParams(), {}, "输入描述", "",
					[this, &cheat = *cheatPtr](CollectTextInputView &view, const char *str)
					{
						if(!system().setCheatName(cheat, str))
						{
							app().postMessage(true, "A cheat with name already exists");
							return true;
						}
						onCheatsChanged();
						view.dismiss();
						return false;
					});
				return false;
			});
	}
};

class BaseEditCheatView : public TableView, public EmuAppHelper
{
public:
	BaseEditCheatView(UTF16Convertible auto &&viewName, ViewAttachParams attach, Cheat& cheat,
		BaseEditCheatsView& editCheatsView_):
		TableView
		{
			IG_forward(viewName),
			attach,
			items
		},
		cheatPtr{&cheat},
		editCheatsView{editCheatsView_},
		name
		{
			system().cheatName(cheat), attach,
			[this](const Input::Event &e)
			{
				pushAndShowNewCollectValueInputView<const char*>(attachParams(), e,
					"输入描述", system().cheatName(*cheatPtr),
					[this](CollectTextInputView&, auto str)
					{
						if(!system().setCheatName(*cheatPtr, str))
						{
							app().postMessage(true, "A cheat with name already exists");
							return false;
						}
						name.compile(str);
						onCheatsChanged();
						postDraw();
						return true;
					});
			}
		},
		remove
		{
			"删除", attach,
			[this](const Input::Event &e)
			{
				pushAndShowModal(makeView<YesNoAlertView>("确定删除此秘籍?",
					YesNoAlertView::Delegates{.onYes = [this]{ removeCheat(); }}), e);
			}
		} {}

	void onCheatsChanged() { editCheatsView.onCheatsChanged(); }

	void removeCheat()
	{
		system().removeCheat(*cheatPtr);
		onCheatsChanged();
		dismiss();
	}

	void removeCheatCode(this auto&& self, CheatCode& c)
	{
		self.cheatPtr = self.system().removeCheatCode(*self.cheatPtr, c);
		self.onCheatsChanged();
		if(!self.cheatPtr)
			self.dismiss();
		else
			self.loadItems();
	}

	bool modifyCheatCode(this auto&& self, CheatCode& c, CheatCodeDesc desc)
	{
		if(!strlen(desc.str))
		{
			self.removeCheatCode(c);
			return true;
		}
		if(!self.system().modifyCheatCode(self.app(), *self.cheatPtr, c, desc))
		{
			self.postDraw();
			return false;
		}
		self.onCheatsChanged();
		self.loadItems();
		self.place();
		self.postDraw();
		return true;
	}

protected:
	Cheat* cheatPtr;
	BaseEditCheatsView& editCheatsView;
	std::vector<MenuItem*> items;
	std::vector<DualTextMenuItem> codes;
	TextMenuItem name, remove;

	void addNewCheatCode(this auto&& self, const char* promptStr, const Input::Event& e, unsigned flags = 0)
	{
		pushAndShowNewCollectTextInputView(self.attachParams(), e, promptStr, "",
			[&self, flags](CollectTextInputView& view, const char* str)
			{
				if(!self.system().addCheatCode(self.app(), self.cheatPtr, {str, flags}))
					return true;
				self.loadItems();
				self.onCheatsChanged();
				view.dismiss();
				return false;
			});
	}
};

}
