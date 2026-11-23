/* Raw - Another World Interpreter
 * Copyright (C) 2004 Gregory Montoir
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version.

 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.

 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA  02111-1307, USA.
 */

#include "video.h"
#include "resource.h"
#include "serializer.h"
#include "systemstub.h"

typedef uint32 __attribute__((may_alias)) u32a;

static uint32 _fontMask[256];

static inline void fillSpan(uint8 *p, int n, uint8 v) {
    while (n > 0 && ((uintptr_t)p & 3)) { *p++ = v; --n; }
    const uint32 v32 = v * 0x01010101u;
    u32a *p32 = (u32a *)p;
    for (int k = n >> 2; k > 0; --k) *p32++ = v32;
    p = (uint8 *)p32;
    for (n &= 3; n > 0; --n) *p++ = v;
}

static inline void orSpan(uint8 *p, int n, uint8 v) {
    while (n > 0 && ((uintptr_t)p & 3)) { *p++ |= v; --n; }
    const uint32 v32 = v * 0x01010101u;
    u32a *p32 = (u32a *)p;
    for (int k = n >> 2; k > 0; --k) *p32++ |= v32;
    p = (uint8 *)p32;
    for (n &= 3; n > 0; --n) *p++ |= v;
}

static inline void copySpan(uint8 *p, const uint8 *q, int n) {
    while (n > 0 && ((uintptr_t)p & 3)) { *p++ = *q++; --n; }
    u32a *p32 = (u32a *)p;
    const u32a *q32 = (const u32a *)q;
    for (int k = n >> 2; k > 0; --k) *p32++ = *q32++;
    p = (uint8 *)p32;
    q = (const uint8 *)q32;
    for (n &= 3; n > 0; --n) *p++ = *q++;
}


void Polygon::init(const uint8 *p, uint16 zoom) {
    bbw = (*p++) * zoom / 64;
    bbh = (*p++) * zoom / 64;
    numPoints = *p++;
    assert((numPoints & 1) == 0 && numPoints < MAX_POINTS);
    for (int i = 0; i < numPoints; ++i) {
        Point *pt = &points[i];
        pt->x = (*p++) * zoom / 64;
        pt->y = (*p++) * zoom / 64;
    }
}

Video::Video(Resource *res, SystemStub *stub) 
    : _res(res), _stub(stub) {
}

void Video::init() {
    _newPal = 0xFF;
    for (int i = 0; i < 4; ++i) {
        _pagePtrs[i] = allocPage();
    }
    _curPagePtr3 = getPagePtr(1);
    _curPagePtr2 = getPagePtr(2);
    changePagePtr1(0xFE);
    _interpTable[0] = 0x4000;
    for (int i = 1; i < 0x400; ++i) {
        _interpTable[i] = 0x4000 / i;
    }

    for (int c = 0; c < 256; ++c) {
        uint32 m = 0;
        for (int i = 0; i < 8; ++i) {
            if (c & (0x80 >> i)) {
                m |= 0xFu << ((i >> 1) * 8 + ((i & 1) ? 0 : 4));
            }
        }
        _fontMask[c] = m;
    }
    
}

void Video::setDataBuffer(uint8 *dataBuf, uint16 offset) {
    _dataBuf = dataBuf;
    _pData.pc = dataBuf + offset;
}

ITCM_CODE void Video::drawShape(uint8 color, uint16 zoom, const Point &pt) {
    uint8 i = _pData.fetchByte();
    if (i >= 0xC0) {
        if (color & 0x80) {
            color = i & 0x3F;
        }
        _pg.init(_pData.pc, zoom);
        fillPolygon(color, zoom, pt);
    } else {
        i &= 0x3F;
        if (i == 1) {
            warning("Video::drawShape() ec=0x%X (i != 2)", 0xF80);
        } else if (i == 2) {
            drawShapeParts(zoom, pt);
        } else {
            warning("Video::drawShape() ec=0x%X (i != 2)", 0xFBB);
        }
    }
}

ITCM_CODE void Video::fillPolygon(uint16 color, uint16 zoom, const Point &pt) {
    if (_pg.bbw == 0 && _pg.bbh == 1 && _pg.numPoints == 4) {
        drawPoint(color, pt.x, pt.y);
        return;
    }
    
    int16 x1 = pt.x - _pg.bbw / 2;
    int16 x2 = pt.x + _pg.bbw / 2;
    int16 y1 = pt.y - _pg.bbh / 2;
    int16 y2 = pt.y + _pg.bbh / 2;

    if (x1 > 319 || x2 < 0 || y1 > 199 || y2 < 0)
        return;

    _hliney = y1;
    
    uint16 i, j;
    i = 0;
    j = _pg.numPoints - 1;
    
    x2 = _pg.points[i].x + x1;
    x1 = _pg.points[j].x + x1;

    ++i;
    --j;

    drawLine pdl;
    if (color < 0x10) {
        pdl = &Video::drawLineN;
    } else if (color > 0x10) {
        pdl = &Video::drawLineP;
    } else {
        pdl = &Video::drawLineT;
    }

    uint32 cpt1 = x1 << 16;
    uint32 cpt2 = x2 << 16;

    while (1) {
        _pg.numPoints -= 2;
        if (_pg.numPoints == 0) {
            return;
        }
        uint16 h;
        int32 step1 = calcStep(_pg.points[j + 1], _pg.points[j], h);
        int32 step2 = calcStep(_pg.points[i - 1], _pg.points[i], h);

        ++i;
        --j;

        cpt1 = (cpt1 & 0xFFFF0000) | 0x7FFF;
        cpt2 = (cpt2 & 0xFFFF0000) | 0x8000;

        if (h == 0) {   
            cpt1 += step1;
            cpt2 += step2;
        } else {
            for (; h != 0; --h) {
                if (_hliney >= 0) {
                    x1 = cpt1 >> 16;
                    x2 = cpt2 >> 16;
                    if (x1 <= 319 && x2 >= 0) {
                        if (x1 < 0) x1 = 0;
                        if (x2 > 319) x2 = 319;
                        (this->*pdl)(x1, x2, color);
                    }
                }
                cpt1 += step1;
                cpt2 += step2;
                ++_hliney;                  
                if (_hliney > 199) return;
            }
        }
    }
}

ITCM_CODE void Video::drawShapeParts(uint16 zoom, const Point &pgc) {
    Point pt(pgc);
    pt.x -= _pData.fetchByte() * zoom / 64;
    pt.y -= _pData.fetchByte() * zoom / 64;
    int16 n = _pData.fetchByte();
    debug(DBG_VIDEO, "Video::drawShapeParts n=%d", n);
    for ( ; n >= 0; --n) {
        uint16 off = _pData.fetchWord();
        Point po(pt);
        po.x += _pData.fetchByte() * zoom / 64;
        po.y += _pData.fetchByte() * zoom / 64;
        uint16 color = 0xFF;
        uint16 _bp = off;
        off &= 0x7FFF;
        if (_bp & 0x8000) {
            color = *_pData.pc & 0x7F;
            _pData.pc += 2;
        }
        uint8 *bak = _pData.pc;
        _pData.pc = _dataBuf + off * 2;
        drawShape(color, zoom, po);
        _pData.pc = bak;
    }
}

ITCM_CODE int32 Video::calcStep(const Point &p1, const Point &p2, uint16 &dy) {
    dy = p2.y - p1.y;
    return (p2.x - p1.x) * _interpTable[dy] * 4;
}

ITCM_CODE void Video::drawString(uint8 color, uint16 x, uint16 y, uint16 strId) {
    const StrEntry *se = _stringsTableEng;
    while (se->id != 0xFFFF && se->id != strId) ++se;
    debug(DBG_VIDEO, "drawString(%d, %d, %d, '%s')", color, x, y, se->str);
    uint16 xx = x;
    int len = strlen(se->str);
    for (int i = 0; i < len; ++i) {
        if (se->str[i] == '\n') {
            y += 8;
            x = xx;
        } else {
            drawChar(se->str[i], x, y, color, _curPagePtr1);
            ++x;
        }
    }
}

ITCM_CODE void Video::drawPoint(uint8 color, int16 x, int16 y) {
    debug(DBG_VIDEO, "drawPoint(%d, %d, %d)", color, x, y);
    if (x >= 0 && x <= 319 && y >= 0 && y <= 199) {
        uint16 off = y * 160 + x / 2;
    
        uint8 cmasko, cmaskn;
        if (x & 1) {
            cmaskn = 0x0F;
            cmasko = 0xF0;
        } else {
            cmaskn = 0xF0;
            cmasko = 0x0F;
        }

        uint8 colb = (color << 4) | color;
        if (color == 0x10) {
            cmaskn &= 0x88;
            cmasko = ~cmaskn;
            colb = 0x88;        
        } else if (color == 0x11) {
            colb = *(_pagePtrs[0] + off);
        }
        uint8 b = *(_curPagePtr1 + off);
        *(_curPagePtr1 + off) = (b & cmasko) | (colb & cmaskn);
    }
}


ITCM_CODE void Video::drawChar(uint8 c, uint16 x, uint16 y, uint8 color, uint8 *buf) {
    if (x <= 39 && y <= 192) {
        const uint8 *ft = _font + (c - 0x20) * 8;
        u32a *p = (u32a *)(buf + x * 4 + y * 160);
        const uint32 col = color * 0x11111111u;
        for (int j = 0; j < 8; ++j) {
            const uint32 m = _fontMask[ft[j]];
            *p = (*p & ~m) | (col & m);
            p += 40; // 160 bytes per row
        }
    }
}

ITCM_CODE void Video::drawLineT(int16 x1, int16 x2, uint8 color) {
    debug(DBG_VIDEO, "drawLineT(%d, %d, %d)", x1, x2, color);
    int xmin = MIN(x1, x2);
    int xmax = MAX(x1, x2);
    uint8 *row = _curPagePtr1 + _hliney * 160;
    uint8 *p = row + (xmin >> 1);
    uint8 *e = row + (xmax >> 1); // last byte, inclusive
    if (xmin & 1)    { *p |= 0x08; ++p; }
    if (!(xmax & 1)) { *e |= 0x80; --e; }
    orSpan(p, e - p + 1, 0x88);
}

ITCM_CODE void Video::drawLineN(int16 x1, int16 x2, uint8 color) {
    debug(DBG_VIDEO, "drawLineN(%d, %d, %d)", x1, x2, color);
    int xmin = MIN(x1, x2);
    int xmax = MAX(x1, x2);
    uint8 *row = _curPagePtr1 + _hliney * 160;
    uint8 *p = row + (xmin >> 1);
    uint8 *e = row + (xmax >> 1);
    const uint8 colb = (color & 0xF) * 0x11;
    if (xmin & 1)    { *p = (*p & 0xF0) | (colb & 0x0F); ++p; }
    if (!(xmax & 1)) { *e = (*e & 0x0F) | (colb & 0xF0); --e; }
    fillSpan(p, e - p + 1, colb);
}

ITCM_CODE void Video::drawLineP(int16 x1, int16 x2, uint8 color) {
    debug(DBG_VIDEO, "drawLineP(%d, %d, %d)", x1, x2, color);
    if (_curPagePtr1 == _pagePtrs[0]) return; // copying page 0 onto itself
    int xmin = MIN(x1, x2);
    int xmax = MAX(x1, x2);
    int off = _hliney * 160;
    uint8 *p = _curPagePtr1 + off + (xmin >> 1);
    uint8 *e = _curPagePtr1 + off + (xmax >> 1);
    const uint8 *q = _pagePtrs[0] + off + (xmin >> 1);
    const uint8 *qe = _pagePtrs[0] + off + (xmax >> 1);
    if (xmin & 1)    { *p = (*p & 0xF0) | (*q & 0x0F); ++p; ++q; }
    if (!(xmax & 1)) { *e = (*e & 0x0F) | (*qe & 0xF0); --e; }
    copySpan(p, q, e - p + 1);
}

ITCM_CODE uint8 *Video::getPagePtr(uint8 page) {
    uint8 *p;
    if (page <= 3) {
        p = _pagePtrs[page];
    } else {
        switch (page) {
        case 0xFF:
            p = _curPagePtr3;
            break;
        case 0xFE:
            p = _curPagePtr2;
            break;
        default:
            p = _pagePtrs[0]; // XXX check
            warning("Video::getPagePtr() p != [0,1,2,3,0xFF,0xFE] == 0x%X", page);
            break;
        }
    }
    return p;
}

ITCM_CODE void Video::changePagePtr1(uint8 page) {
    debug(DBG_VIDEO, "Video::changePagePtr1(%d)", page);
    _curPagePtr1 = getPagePtr(page);
}

ITCM_CODE void Video::fillPage(uint8 page, uint8 color) {
    debug(DBG_VIDEO, "Video::fillPage(%d, %d)", page, color);
    uint8 *p = getPagePtr(page);
    uint8 c = (color << 4) | color;
    memset(p, c, VID_PAGE_SIZE);
}

ITCM_CODE void Video::copyPage(uint8 src, uint8 dst, int16 vscroll) {
    debug(DBG_VIDEO, "Video::copyPage(%d, %d)", src, dst);
    if (src >= 0xFE || !((src &= 0xBF) & 0x80)) {
        uint8 *p = getPagePtr(src);
        uint8 *q = getPagePtr(dst);
        if (p != q) {
            memcpy(q, p, VID_PAGE_SIZE);
        }       
    } else {
        uint8 *p = getPagePtr(src & 3);
        uint8 *q = getPagePtr(dst);
        if (p != q && vscroll >= -199 && vscroll <= 199) {
            uint16 h = 200;
            if (vscroll < 0) {
                h += vscroll;
                p += -vscroll * 160;
            } else {
                h -= vscroll;
                q += vscroll * 160;
            }
            memcpy(q, p, h * 160);
        }
    }
}

ITCM_CODE void Video::copyPagePtr(const uint8 *src) {
    debug(DBG_VIDEO, "Video::copyPagePtr()");
    uint8 *dst = _pagePtrs[0];
    int h = 200;
    while (h--) {
        int w = 40;
        while (w--) {
            uint8 p[] = {
                *(src + 8000 * 3),
                *(src + 8000 * 2),
                *(src + 8000 * 1),
                *(src + 8000 * 0)
            };
            for(int j = 0; j < 4; ++j) {
                uint8 acc = 0;
                for (int i = 0; i < 8; ++i) {
                    acc <<= 1;
                    acc |= (p[i & 3] & 0x80) ? 1 : 0;
                    p[i & 3] <<= 1;
                }
                *dst++ = acc;
            }
            ++src;
        }
    }
}

ITCM_CODE uint8 *Video::allocPage() {
    uint8 *buf = (uint8 *)malloc(VID_PAGE_SIZE);
    memset(buf, 0, VID_PAGE_SIZE);
    return buf;
}

ITCM_CODE void Video::changePal(uint8 palNum) {
    if (palNum < 32) {
        uint8 *p = _res->_segVideoPal + palNum * 32;
        uint8 pal[16 * 3];
        for (int i = 0; i < 16; ++i) {
            uint8 c1 = *(p + 0);
            uint8 c2 = *(p + 1);
            p += 2;
            pal[i * 3 + 0] = ((c1 & 0x0F) << 2) | ((c1 & 0x0F) >> 2); // r
            pal[i * 3 + 1] = ((c2 & 0xF0) >> 2) | ((c2 & 0xF0) >> 6); // g
            pal[i * 3 + 2] = ((c2 & 0x0F) >> 2) | ((c2 & 0x0F) << 2); // b
        }
        _stub->setPalette(0, 16, pal);
        _curPal = palNum;
    }
}

ITCM_CODE void Video::updateDisplay(uint8 page) {
    debug(DBG_VIDEO, "Video::updateDisplay(%d)", page);
    if (page != 0xFE) {
        if (page == 0xFF) {
            SWAP(_curPagePtr2, _curPagePtr3);
        } else {
            _curPagePtr2 = getPagePtr(page);
        }
    }
    if (_newPal != 0xFF) {
        changePal(_newPal);
        _newPal = 0xFF;
    }
    _stub->copyRect(0, 0, 320, 200, _curPagePtr2, 160);
}

void Video::saveOrLoad(Serializer &ser) {
    uint8 mask = 0;
    if (ser._mode == Serializer::SM_SAVE) {
        for (int i = 0; i < 4; ++i) {
            if (_pagePtrs[i] == _curPagePtr1)
                mask |= i << 4;
            if (_pagePtrs[i] == _curPagePtr2)
                mask |= i << 2;
            if (_pagePtrs[i] == _curPagePtr3)
                mask |= i << 0;
        }       
    }
    Serializer::Entry entries[] = {
        SE_INT(&_curPal, Serializer::SES_INT8, VER(1)),
        SE_INT(&_newPal, Serializer::SES_INT8, VER(1)),
        SE_INT(&mask, Serializer::SES_INT8, VER(1)),
        SE_ARRAY(_pagePtrs[0], Video::VID_PAGE_SIZE, Serializer::SES_INT8, VER(1)),
        SE_ARRAY(_pagePtrs[1], Video::VID_PAGE_SIZE, Serializer::SES_INT8, VER(1)),
        SE_ARRAY(_pagePtrs[2], Video::VID_PAGE_SIZE, Serializer::SES_INT8, VER(1)),
        SE_ARRAY(_pagePtrs[3], Video::VID_PAGE_SIZE, Serializer::SES_INT8, VER(1)),
        SE_END()
    };
    ser.saveOrLoadEntries(entries);
    if (ser._mode == Serializer::SM_LOAD) {
        _curPagePtr1 = _pagePtrs[(mask >> 4) & 0x3];
        _curPagePtr2 = _pagePtrs[(mask >> 2) & 0x3];
        _curPagePtr3 = _pagePtrs[(mask >> 0) & 0x3];
        changePal(_curPal);
    }
}
