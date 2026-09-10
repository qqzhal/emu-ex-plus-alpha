/*  This file is part of NES.emu.

	NES.emu is free software: you can redistribute it and/or modify
	it under the terms of the GNU General Public License as published by
	the Free Software Foundation, either version 3 of the License, or
	(at your option) any later version.

	NES.emu is distributed in the hope that it will be useful,
	but WITHOUT ANY WARRANTY; without even the implied warranty of
	MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
	GNU General Public License for more details.

	You should have received a copy of the GNU General Public License
	along with NES.emu.  If not, see <http://www.gnu.org/licenses/> */

#include <imagine/gui/TextEntry.hh>
#include <imagine/util/string.h>
#include <imagine/util/format.hh>
#include <imagine/logger/logger.h>
#include <emuframework/Cheats.hh>
#include <emuframework/EmuApp.hh>
#include <emuframework/viewUtils.hh>
#include "EmuCheatViews.hh"
#include "MainSystem.hh"
#include <fceu/driver.h>
#include <fceu/cheat.h>
#include <algorithm>
#include <cstdio>
#include "gbk_table.inc"

void EncodeGG(char *str, int a, int v, int c);
void RebuildSubCheats();

namespace EmuEx
{

constexpr SystemLogger log{"NES.emu"};

static unsigned parseHex(const char* str) { return strtoul(str, nullptr, 16); }

constexpr bool isValidGGCodeLen(const char* str)
{
	return std::string_view{str}.size() == 6 || std::string_view{str}.size() == 8;
}

static void saveCheats()
{
	savecheats = 1;
	FCEU_FlushGameCheats(nullptr, 0, false);
}

static void syncCheats()
{
	saveCheats();
	RebuildSubCheats();
}

Cheat* NesSystem::newCheat(EmuApp& app, const char* name, CheatCodeDesc desc)
{
	auto cPtr = &static_cast<Cheat&>(cheats.emplace_back(name));
	if(!addCheatCode(app, cPtr, desc))
	{
		cheats.pop_back();
		return {};
	}
	log.info("added new cheat, {} total", cheats.size());
	return cPtr;
}

bool NesSystem::setCheatName(Cheat& c, const char* name)
{
	c.name = name;
	saveCheats();
	return true;
}

std::string_view NesSystem::cheatName(const Cheat& c) const { return c.name; }

void NesSystem::setCheatEnabled(Cheat& c, bool on)
{
	c.status = on;
	syncCheats();
}

bool NesSystem::isCheatEnabled(const Cheat& c) const { return c.status; }

bool NesSystem::addCheatCode(EmuApp& app, Cheat*& cheatPtr, CheatCodeDesc desc)
{
	if(desc.flags)
	{
		if(!isValidGGCodeLen(desc.str))
		{
			app.postMessage(true, "无效,必须为6位或8位");
			return false;
		}
		uint16 a; uint8 v; int c;
		if(!FCEUI_DecodeGG(desc.str, &a, &v, &c))
		{
			app.postMessage(true, "错误解码代码");
			return false;
		}
		cheatPtr->codes.emplace_back(a, v, c, 1);
	}
	else
	{
		auto a = parseHex(desc.str);
		if(a > 0xFFFF)
		{
			app.postMessage(true, "无效地址");
			return false;
		}
		cheatPtr->codes.emplace_back(a, 0, -1, 0);
	}
	syncCheats();
	return true;
}

bool NesSystem::modifyCheatCode(EmuApp& app, Cheat&, CheatCode& c, CheatCodeDesc desc)
{
	assert(desc.flags);
	if(!isValidGGCodeLen(desc.str))
	{
		app.postMessage(true, "无效,必须为6位或8位");
		return false;
	}
	if(!FCEUI_DecodeGG(desc.str, &c.addr, &c.val, &c.compare))
	{
		app.postMessage(true, "错误解码代码");
		return false;
	}
	syncCheats();
	return true;
}

Cheat* NesSystem::removeCheatCode(Cheat& c, CheatCode& code)
{
	c.codes.erase(toIterator(c.codes, static_cast<CHEATCODE&>(code)));
	bool removedAllCodes = c.codes.empty();
	if(removedAllCodes)
		cheats.erase(toIterator(cheats, static_cast<CHEATF&>(c)));
	syncCheats();
	return removedAllCodes ? nullptr : &c;
}

bool NesSystem::removeCheat(Cheat& c)
{
	cheats.erase(toIterator(cheats, static_cast<CHEATF&>(c)));
	syncCheats();
	return true;
}

void NesSystem::forEachCheat(DelegateFunc<bool(Cheat&, std::string_view)> del)
{
	for(auto& c: cheats)
	{
		if(!del(static_cast<Cheat&>(c), std::string_view{c.name}))
			break;
	}
}

static std::string toGGString(const CheatCode& c)
{
	std::string code;
	code.resize(9);
	EncodeGG(code.data(), c.addr, c.val, c.compare);
	code.resize(8);
	return code;
}

void NesSystem::forEachCheatCode(Cheat& cheat, DelegateFunc<bool(CheatCode&, std::string_view)> del)
{
	for(auto& c_: cheat.codes)
	{
		auto& c = static_cast<CheatCode&>(c_);
		std::string code;
		if(c.type)
		{
			code = toGGString(c);
		}
		else
		{
			code = std::format("{:x}:{:x}", c.addr, c.val);
			if(c.compare != -1)
				code += std::format(":{:x}", c.compare);
		}
		del(c, std::string_view{code});
	}
}

static std::string codeCompareToString(int compare) { return compare != -1 ? std::format("{:x}", compare) : std::string{}; }

EditRamCheatView::EditRamCheatView(ViewAttachParams attach, Cheat& cheat_, CheatCode& code_, EditCheatView& editCheatView_):
	TableView
	{
		"编辑内存补丁",
		attach,
		[this](ItemMessage msg) -> ItemReply
		{
			return msg.visit(overloaded
			{
				[&](const ItemsMessage &m) -> ItemReply { return 4u; },
				[&](const GetItemMessage &m) -> ItemReply
				{
					switch(m.idx)
					{
						case 0: return &addr;
						case 1: return &value;
						case 2: return &comp;
						case 3: return &remove;
						default: std::unreachable();
					}
				},
			});
		}
	},
	cheat{cheat_},
	code{code_},
	editCheatView{editCheatView_},
	addr
	{
		"Address",
		std::format("{:x}", code_.addr),
		attach,
		[this](const Input::Event& e)
		{
			pushAndShowNewCollectValueInputView<const char*>(attachParams(), e, "输入4位16进制数值", std::format("{:x}", code.addr),
				[this](CollectTextInputView&, auto str)
				{
					unsigned a = parseHex(str);
					if(a > 0xFFFF)
					{
						app().postMessage(true, "无效输入");
						return false;
					}
					code.addr = a;
					syncCheats();
					addr.set2ndName(str);
					addr.place();
					editCheatView.loadItems();
					return true;
				});
		}
	},
	value
	{
		"Value",
		std::format("{:x}", code_.val),
		attach,
		[this](const Input::Event& e)
		{
			pushAndShowNewCollectValueInputView<const char*>(attachParams(), e, "输入2位16进制数值", std::format("{:x}", code.val),
				[this](CollectTextInputView&, auto str)
				{
					unsigned a = parseHex(str);
					if(a > 0xFF)
					{
						app().postMessage(true, "无效数值");
						return false;
					}
					code.val = a;
					syncCheats();
					value.set2ndName(str);
					value.place();
					editCheatView.loadItems();
					return true;
				});
		}
	},
	comp
	{
		"对比",
		codeCompareToString(code_.compare),
		attach,
		[this](const Input::Event& e)
		{
			pushAndShowNewCollectValueInputView<const char*, ScanValueMode::AllowBlank>(attachParams(), e, "输入2位16进制或空白", codeCompareToString(code.compare),
				[this](CollectTextInputView &, const char *str)
				{
					if(strlen(str))
					{
						unsigned a = parseHex(str);
						if(a > 0xFF)
						{
							app().postMessage(true, "无效数值");
							return true;
						}
						code.compare = a;
						comp.set2ndName(str);
					}
					else
					{
						code.compare = -1;
						comp.set2ndName();
					}
					syncCheats();
					comp.place();
					editCheatView.loadItems();
					return true;
				});
		}
	},
	remove
	{
		"删除", attach,
		[this](const Input::Event& e)
		{
			pushAndShowModal(makeView<YesNoAlertView>("确定删除此补丁?",
				YesNoAlertView::Delegates{.onYes = [this]{ editCheatView.removeCheatCode(code); dismiss(); }}), e);
		}
	} {}

EditCheatView::EditCheatView(ViewAttachParams attach, Cheat& cheat, BaseEditCheatsView& editCheatsView):
	BaseEditCheatView
	{
		"编辑秘籍",
		attach,
		cheat,
		editCheatsView
	},
	addGG
	{
		"添加其他代码", attach,
		[this](const Input::Event& e) { addNewCheatCode("输入金手指代码", e, 1); }
	},
	addRAM
	{
		"添加其他补丁", attach,
		[this](const Input::Event& e) { addNewCheatCode("输入RAM十六进制地址", e, 0); }
	}
{
	loadItems();
}

void EditCheatView::loadItems()
{
	codes.clear();
	system().forEachCheatCode(*cheatPtr, [this](CheatCode& c, std::string_view code)
	{
		codes.emplace_back("Code", code, attachParams(), [this, &c](const Input::Event& e)
		{
			if(c.type)
			{
				pushAndShowNewCollectValueInputView<const char*, ScanValueMode::AllowBlank>(attachParams(), e, "输入金手指代码", toGGString(c),
					[this, &c](CollectTextInputView&, auto str) { return modifyCheatCode(c, {str, 1}); });
			}
			else
			{
				pushAndShow(makeView<EditRamCheatView>(*cheatPtr, c, *this), e);
			}
		});
		return true;
	});
	items.clear();
	items.emplace_back(&name);
	for(auto& c: codes)
	{
		items.emplace_back(&c);
	}
	items.emplace_back(&addGG);
	items.emplace_back(&addRAM);
	items.emplace_back(&remove);
}

EditCheatsView::EditCheatsView(ViewAttachParams attach, CheatsView& cheatsView):
	BaseEditCheatsView
	{
		attach,
		cheatsView,
		[this](ItemMessage msg) -> ItemReply
		{
			return msg.visit(overloaded
			{
				[&](const ItemsMessage &m) -> ItemReply { return 2 + ::cheats.size(); },
				[&](const GetItemMessage &m) -> ItemReply
				{
					switch(m.idx)
					{
						case 0: return &addGG;
						case 1: return &addRAM;
						default: return &cheats[m.idx - 2];
					}
				},
			});
		}
	},
	addGG
	{
		"添加金手指代码", attachParams(),
		[this](const Input::Event& e) { addNewCheat("输入金手指代码", e, 1); }
	},
	addRAM
	{
		"添加内存补丁", attachParams(),
		[this](const Input::Event& e) { addNewCheat("输入RAM十六进制地址", e, 0); }
	} {}

static bool validGBK(std::string_view s)
{
	for(size_t i = 0; i < s.size();)
	{
		auto b = (unsigned char)s[i];
		if(b < 0x80)
		{
			i++;
			continue;
		}
		if(b < 0x81 || b > 0xFE || i + 1 >= s.size())
			return false;
		auto b2 = (unsigned char)s[i + 1];
		if(b2 < 0x40 || b2 > 0xFE || b2 == 0x7F)
			return false;
		if(!gbkToUnicode[(size_t)(b - 0x81) * 190 + (b2 - 0x40 - (b2 > 0x7F ? 1 : 0))])
			return false;
		i += 2;
	}
	return true;
}

static std::string gbkToUTF8(std::string_view s)
{
	std::string out;
	for(size_t i = 0; i < s.size();)
	{
		auto b = (unsigned char)s[i];
		if(b < 0x80)
		{
			out.push_back(b);
			i++;
			continue;
		}
		auto b2 = (unsigned char)s[i + 1];
		auto cp = gbkToUnicode[(size_t)(b - 0x81) * 190 + (b2 - 0x40 - (b2 > 0x7F ? 1 : 0))];
		if(cp < 0x80)
			out.push_back(cp);
		else if(cp < 0x800)
		{
			out.push_back(0xC0 | (cp >> 6));
			out.push_back(0x80 | (cp & 0x3F));
		}
		else
		{
			out.push_back(0xE0 | (cp >> 12));
			out.push_back(0x80 | ((cp >> 6) & 0x3F));
			out.push_back(0x80 | (cp & 0x3F));
		}
		i += 2;
	}
	return out;
}

// 解析 VirtuaNES 系 .cht 金手指文件并导入为普通秘籍条目。
// 文件格式: UTF-16LE 或 UTF-8 文本，[组名] 分组，组下每行 "选项名=地址,值[,高字节];..."。
// 两段为单字节写；三段为 16 位值按小端写两个连续字节（addr=低, addr+1=高，
// 已实测确认，如 "454,10,02" 即 454=0x10、455=0x02，合成 0x0210=528）。
// 每个选项行导入为一条秘籍（多补丁挂在同一条目下），名字为 "组名 · 选项名"，
// 单选项组（仅 ON 一行）直接用组名。界面上同组条目聚合为单选（见 Cheats.hh）。
int NesSystem::importCheatsFile(EmuApp& app, CStringView pathStr)
{
	std::FILE *f = fopen(std::string{pathStr.data(), pathStr.size()}.c_str(), "rb");
	if(!f)
	{
		app.postErrorMessage("无法打开秘籍文件");
		return -1;
	}
	std::vector<uint8_t> data;
	{
		char buf[8192];
		size_t n;
		while((n = fread(buf, 1, sizeof(buf), f)))
			data.insert(data.end(), buf, buf + n);
	}
	fclose(f);
	if(data.size() < 4)
	{
		app.postErrorMessage("秘籍文件内容为空");
		return -1;
	}

	auto validUTF8 = [](std::string_view s)
	{
		for(size_t i = 0; i < s.size();)
		{
			auto c = (unsigned char)s[i];
			int len = (c & 0xE0) == 0xC0 ? 2 : (c & 0xF0) == 0xE0 ? 3 : (c & 0xF8) == 0xF0 ? 4 : 0;
			if(!len || i + len > s.size())
				return false;
			for(int j = 1; j < len; j++)
				if(((unsigned char)s[i + j] & 0xC0) != 0x80)
					return false;
			i += len;
		}
		return true;
	};
	std::string text;
	if(data[0] == 0xFF && data[1] == 0xFE) // UTF-16LE BOM
	{
		for(size_t i = 2; i + 1 < data.size(); i += 2)
		{
			unsigned cp = data[i] | (data[i + 1] << 8);
			if(cp >= 0xD800 && cp < 0xDC00 && i + 3 < data.size())
			{
				unsigned lo = data[i + 2] | (data[i + 3] << 8);
				if(lo >= 0xDC00 && lo < 0xE000)
				{
					cp = 0x10000 + ((cp - 0xD800) << 10) + (lo - 0xDC00);
					i += 2;
				}
			}
			if(cp < 0x80) text.push_back(cp);
			else if(cp < 0x800)
			{
				text.push_back(0xC0 | (cp >> 6));
				text.push_back(0x80 | (cp & 0x3F));
			}
			else if(cp < 0x10000)
			{
				text.push_back(0xE0 | (cp >> 12));
				text.push_back(0x80 | ((cp >> 6) & 0x3F));
				text.push_back(0x80 | (cp & 0x3F));
			}
			else
			{
				text.push_back(0xF0 | (cp >> 18));
				text.push_back(0x80 | ((cp >> 12) & 0x3F));
				text.push_back(0x80 | ((cp >> 6) & 0x3F));
				text.push_back(0x80 | (cp & 0x3F));
			}
		}
	}
	else if(data[0] == 0xEF && data[1] == 0xBB && data[2] == 0xBF)
		text.assign((char*)data.data() + 3, data.size() - 3);
	else if(validUTF8({(char*)data.data(), data.size()}))
		text.assign((char*)data.data(), data.size());
	else if(validGBK({(char*)data.data(), data.size()}))
		text = gbkToUTF8({(char*)data.data(), data.size()});
	else
	{
		app.postErrorMessage("无法识别秘籍文件编码，请用记事本另存为 UTF-16 或 ANSI");
		return -1;
	}

	// 规范化中文输入法常见的全角标点 (逗号/分号/等号/空格) 为半角, 避免手编文件解析失败
	auto replaceAll = [](std::string &s, std::string_view from, std::string_view to)
	{
		if(from.empty())
			return;
		for(size_t p = 0; (p = s.find(from, p)) != std::string::npos; p += to.size())
			s.replace(p, from.size(), to);
	};
	replaceAll(text, "，", ",");
	replaceAll(text, "；", ";");
	replaceAll(text, "＝", "=");
	replaceAll(text, "　", " ");

	auto trim = [](std::string_view s)
	{
		while(!s.empty() && (s.front() == ' ' || s.front() == '\t' || s.front() == '\r'))
			s.remove_prefix(1);
		while(!s.empty() && (s.back() == ' ' || s.back() == '\t' || s.back() == '\r'))
			s.remove_suffix(1);
		return s;
	};
	auto hexVal = [&](std::string_view s) -> long
	{
		char *end = nullptr;
		long v = strtol(std::string{s}.c_str(), &end, 16);
		return end == std::string{s}.c_str() ? -1 : v;	// 未转换到任何数字则无效
	};
	struct Entry { std::string name; std::vector<CheatCode> codes; };
	std::vector<Entry> entries;
	std::string group;
	size_t lineStart = 0;
	for(size_t pos = 0; pos <= text.size(); pos++)
	{
		if(pos != text.size() && text[pos] != '\n')
			continue;
		auto line = trim(std::string_view{text}.substr(lineStart, pos - lineStart));
		lineStart = pos + 1;
		if(line.empty())
			continue;
		if(line.front() == '[' && line.back() == ']')
		{
			group = std::string{trim(line.substr(1, line.size() - 2))};
			continue;
		}
		auto eq = line.find('=');
		if(eq == std::string_view::npos || group.empty())
			continue;
		auto option = trim(line.substr(0, eq));
		Entry e;
		e.name = (option.empty() || option == "ON") ? group : std::format("{}{}{}", group, cheatGroupSep, option);
		auto codesStr = line.substr(eq + 1);
		size_t segStart = 0;
		for(size_t segPos = 0; segPos <= codesStr.size(); segPos++)
		{
			if(segPos != codesStr.size() && codesStr[segPos] != ';')
				continue;
			auto seg = trim(codesStr.substr(segStart, segPos - segStart));
			segStart = segPos + 1;
			if(seg.empty())
				continue;
			auto c1 = seg.find(',');
			if(c1 == std::string_view::npos)
				continue;
			auto addrS = trim(seg.substr(0, c1));
			auto rest = seg.substr(c1 + 1);
			if(addrS.empty())
				continue;
			unsigned addr = hexVal(addrS);
			if(addr > 0xFFFF)
				continue;
			// 首段为起始地址, 其余每段依次写连续地址 (addr, addr+1, addr+2, ...),
			// 段数可变; 恒为无条件写 (该格式无比较值语义)
			std::string_view byteSeg = rest;
			unsigned cur = addr;
			while(!byteSeg.empty())
			{
				auto comma = byteSeg.find(',');
				auto byteS = trim(comma == std::string_view::npos ? byteSeg : byteSeg.substr(0, comma));
				auto byteVal = byteS.empty() ? -1L : hexVal(byteS);
				if(byteVal >= 0 && byteVal <= 0xFF && cur <= 0xFFFF)
					e.codes.emplace_back(cur, byteVal, -1, 0);
				if(comma == std::string_view::npos)
					break;
				byteSeg = byteSeg.substr(comma + 1);
				cur++;
			}
		}
		if(!e.codes.empty())
			entries.push_back(std::move(e));
	}
	if(entries.empty())
	{
		app.postErrorMessage("文件中没有找到有效的秘籍条目");
		return -1;
	}

	int added = 0, skipped = 0;
	for(auto &e : entries)
	{
		bool exists = std::any_of(cheats.begin(), cheats.end(),
			[&](auto &c) { return c.name == e.name; });
		if(exists)
		{
			skipped++;
			continue;
		}
		auto &c = cheats.emplace_back(e.name);
		for(auto &code : e.codes)
			c.codes.emplace_back(code.addr, code.val, code.compare, code.type);
		added++;
	}
	if(added)
		syncCheats();
	app.postMessage(added != 0, std::format("已导入 {} 条秘籍{}", added,
		skipped ? std::format("，跳过 {} 条同名", skipped) : std::string{}));
	return added;
}

}
