#pragma once

#include "battlemenu.hpp"

// Port-specific headers
#include <SDL3/SDL.h>

// Retail headers
#include "btactstatus.hpp"
#include "camera.hpp"
#include "caption_text.hpp"
#include "character.hpp"
#include "clsmes.hpp"
#include "dataread.hpp"
#include "dngstatusdata.hpp"
#include "draw2d_port.hpp"
#include "dun/gameloop.hpp"
#include "eastking.hpp"
#include "gamepad.hpp"
#include "itemdata.hpp"
#include "localize.hpp"
#include "mainselect.hpp"
#include "map.hpp"
#include "memcard.hpp"
#include "menu_draw.hpp"
#include "menu_dungeon.hpp"
#include "menu_inventory.hpp"
#include "menu_manual.hpp"
#include "menu_misc.hpp"
#include "menu_mouse.hpp"
#include "menu_save.hpp"
#include "menuetc.hpp"
#include "mglib.hpp"
#include "rect.hpp"
#include "snd.hpp"
#include "texture.hpp"
#include "userstatus.hpp"
#include "weapon_buildup.hpp"
#include "weaponlevelup.hpp"

// UI Framework
#include "ui_framework.hpp"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <string>
#include <string_view>
#include <vector>

extern void Btl_InitMenuChara(u_long128 *buffer);
extern void Btl_InitWeaponSelect(int mode, int chara);
extern void Btl_InitItemMode(int mode, int chara);
extern void Btl_BattleManualInit(int*, u_long128*);

#define BTLMENU_PAGE_LOAD 99
#define BTLMENU_PAGE_QUIT 100

namespace btlmenu_detail {

inline void RefreshBtlMenuTextures() {
    MenuCharaFace = TexManager.GetTexture((char*)"charaface", BtlMenuReadBlock);
    BtStatus      = TexManager.GetTexture((char*)"btstatus2", BtlMenuReadBlock);
    WepIcon       = TexManager.GetTexture((char*)"wepicon", -1);
    ItemIcon      = TexManager.GetTexture((char*)"itemicon", BtlMenuReadBlock);
    PerBoardTex   = TexManager.GetTexture((char*)"perbrd", BtlMenuReadBlock);
#ifdef PAL
    WepStatus     = TexManager.GetTexture((char*)"wepstatus", BtlMenuExtBlock);
#else
    WepStatus     = TexManager.GetTexture((char*)"wepstatus", BtlMenuReadBlock);
#endif
    VillageName   = TexManager.GetTexture((char*)"vilname", -1);
    VillageBar    = TexManager.GetTexture((char*)"viltag", -1);
}

inline std::string CleanDecodedText(std::string decoded) {
    for (size_t pos = 0; (pos = decoded.find('{', pos)) != std::string::npos;) {
        size_t end = decoded.find('}', pos);
        if (end != std::string::npos) {
            decoded.erase(pos, end - pos + 1);
        } else {
            break;
        }
    }
    while (!decoded.empty() && (decoded.back() == '\n' || decoded.back() == '\r' || decoded.back() == ' ')) {
        decoded.pop_back();
    }
    return decoded;
}

inline std::string DecodeMesFromBuffer(short* mes_buffer, int mes_no) {
    if (mes_buffer == nullptr || mes_no < 0) return {};
    short* saved_buff   = CommonMenuMes3.buff;
    CommonMenuMes3.buff = mes_buffer;
    const short* codes  = CommonMenuMes3.GetTextLineDataTop(mes_no);
    CommonMenuMes3.buff = saved_buff;
    if (codes == nullptr) return {};
    return CleanDecodedText(GameTextDecode(codes));
}

inline std::string DecodeItemNamePort(int item_no, std::string_view fallback = {}) {
    if (item_no > 0 && SystemMes != nullptr) {
        if (COM_ITEM_INFO* info = GetCommonItemInfo(item_no)) {
            if (info->msg > 0) {
                std::string name = DecodeMesFromBuffer(SystemMes, info->msg + 0x64);
                size_t nl = name.find('\n');
                if (nl != std::string::npos) {
                    name.resize(nl);
                }
                if (!name.empty()) {
                    return name;
                }
            }
        }
    }
    return std::string(fallback);
}

} // namespace btlmenu_detail