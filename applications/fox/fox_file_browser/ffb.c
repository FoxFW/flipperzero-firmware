#include <furi.h>
#include <furi_hal.h>
#include <ctype.h>
#include <gui/gui.h>
#include <gui/view_dispatcher.h>
#include <gui/modules/file_browser.h>
#include <gui/modules/text_input.h>
#include <gui/modules/dialog_ex.h>
#include <gui/icon_i.h>
#include <storage/storage.h>
#include <loader/loader.h>
#include <dialogs/dialogs.h>
#include "ffb_icons.h"
#include "fox_scroll_text.h"

typedef enum {
    ViewStart      = 0,
    ViewBrowser    = 1,
    ViewFavourites = 2,
    ViewMenu       = 3,
    ViewTextInput  = 4,
    ViewImage      = 5,
    ViewText       = 6,
} FFVViewId;

typedef enum { MenuRun=0, MenuPin=1, MenuRename=2, MenuDelete=3 } FFVMenuAction;

typedef enum { StartItemBrowse=0, StartItemSearch=1, StartItemFavs=2 } FFVStartItem;

typedef enum { ListSrcFavorites=0, ListSrcSearch=1 } FFVListSource;

typedef enum { TextInputRename=0, TextInputSearch=1 } FFVTextInputMode;

typedef enum {
    TypeFolder, TypeSubGhz, TypeSubGhzRemote, TypeNfc, TypeInfrared,
    TypeLFRFID, TypeT5577, TypeProtoPirate, TypePicopass, TypeBadUsb, TypeIButton, TypeU2F, TypeApp, TypeUpdate,
    TypeScript, TypeSettings, TypeMusic, TypeFile, TypeText, TypeImage, TypeBmp, TypeUnknown,
} FFVFileType;

#define ROW_HEADER_H  14
#define ROW_ITEM_H    12
#define ROW_VISIBLE    4
#define MENU_MAX_ITEMS 4
#define MENU_TOAST_MS   1800

#define FAV_ROW_H        22
#define FAV_ROW_VIS       2
#define FAV_SCROLL_MS  50

#define SEARCH_MAX_RESULTS 200
#define SEARCH_NAME_MAX    128

#define IMG_W     128
#define IMG_H     64
#define IMG_BYTES ((IMG_W / 8) * IMG_H)

#define TXT_CAP        16384
#define TXT_MAX_LINES  1200
#define TXT_WRAP_COLS  24
#define TXT_ROWS_VIS   6

typedef struct {
    Gui*            gui;
    ViewDispatcher* view_dispatcher;
    View*           start_view;
    FileBrowser*    browser;
    View*           fav_view;
    View*           menu_view;
    TextInput*      text_input;
    FFVTextInputMode text_input_mode;

    FuriString*     selected_path;
    FuriString*     current_dir;
    char            rename_buf[128];
    char            search_query[SEARCH_NAME_MAX];

    Storage*        storage;
    Loader*         loader;
    DialogsApp*     dialogs;
    FFVViewId       current_view;

    FFVStartItem    start_items[3];
    uint8_t         start_count;
    uint8_t         start_selected;
    bool            browser_started;

    char            menu_labels[MENU_MAX_ITEMS][24];
    FFVMenuAction   menu_actions[MENU_MAX_ITEMS];
    uint8_t         menu_count;
    uint8_t         menu_selected;
    bool            menu_from_favs;

    bool            menu_is_app_picker;
    char            app_picker_targets[MENU_MAX_ITEMS][48];

    bool            menu_toast_active;
    FuriTimer*      menu_toast_timer;

    FFVListSource   list_source;
    bool            searching;
    FuriTimer*      fav_scroll_timer;
    FoxScrollText   fav_text_anim;
    FuriString**    fav_list;
    size_t          fav_count;
    size_t          fav_selected;
    size_t          fav_scroll;

    View*           image_view;
    FuriString**    image_list;
    size_t          image_count;
    size_t          image_index;
    bool            image_loaded;
    bool            image_from_favs;
    uint8_t         image_bitmap[IMG_BYTES];

    View*           text_view;
    char*           text_buf;
    size_t          text_len;
    size_t*         text_lines;
    size_t          text_line_count;
    size_t          text_top;
    bool            text_from_favs;
} FFVApp;

static FFVApp* s_ffv_ctx = NULL;

static void ffv_go_browse(FFVApp* app);
static void ffv_go_favs(FFVApp* app);
static void ffv_go_start(FFVApp* app);
static void ffv_refresh_browser(FFVApp* app);
static void ffv_return_from_menu(FFVApp* app);
static void ffv_build_menu(FFVApp* app);
static void ffv_do_action(FFVApp* app, FFVMenuAction action);
static void ffv_favs_load(FFVApp* app);
static void ffv_favs_free(FFVApp* app);
static void ffv_go_search_input(FFVApp* app);
static void ffv_start_build_items(FFVApp* app);
static bool ffv_favorites_non_empty(FFVApp* app);
static void ffv_search_done(void* ctx);
static void ffv_go_image_view(FFVApp* app);
static void ffv_image_list_free(FFVApp* app);
static void ffv_go_text_view(FFVApp* app);
static void ffv_text_free(FFVApp* app);

static FFVFileType ffv_type_from_path(const char* path) {
    const char* s = strrchr(path, '/');
    const char* n = s ? s+1 : path;
    const char* d = strrchr(n, '.');
    if(!d) return TypeFolder;
    if(!strcmp(d,".sub"))                              return TypeSubGhz;
    if(!strcmp(d,".rem"))                              return TypeSubGhzRemote;
    if(!strcmp(d,".nfc"))                              return TypeNfc;
    if(!strcmp(d,".ir"))                               return TypeInfrared;
    if(!strcmp(d,".rfid")||!strcmp(d,".lfrfid"))       return TypeLFRFID;
    if(!strcmp(d,".t5577"))                            return TypeT5577;
    if(!strcmp(d,".psf"))                              return TypeProtoPirate;
    if(!strcmp(d,".picopass")||!strcmp(d,".pas"))      return TypePicopass;
    if(!strcmp(d,".bad"))                              return TypeBadUsb;
    if(!strcmp(d,".ibtn")||!strcmp(d,".ibutton"))      return TypeIButton;
    if(!strcmp(d,".u2f"))                              return TypeU2F;
    if(!strcmp(d,".fap"))                              return TypeApp;
    if(!strcmp(d,".fuf")||!strcmp(d,".tgz")||!strcmp(d,".tar")) return TypeUpdate;
    if(!strcmp(d,".jss")||!strcmp(d,".js"))            return TypeScript;

    if(!strcmp(d,".mp3"))                              return TypeMusic;
    if(!strcmp(d,".xbm"))                              return TypeImage;
    if(!strcmp(d,".bmp"))                              return TypeBmp;
    if(!strcmp(d,".txt")||!strcmp(d,".csv"))           return TypeFile;
    if(!strcmp(d,".md")||!strcmp(d,".log")||!strcmp(d,".json")||
       !strcmp(d,".ini")||!strcmp(d,".cfg")||!strcmp(d,".conf")||
       !strcmp(d,".xml")||!strcmp(d,".yml")||!strcmp(d,".yaml")||
       !strcmp(d,".toml")||!strcmp(d,".c")||!strcmp(d,".h")||
       !strcmp(d,".cpp")||!strcmp(d,".hpp")||!strcmp(d,".py")||
       !strcmp(d,".sh")||!strcmp(d,".ver")||!strcmp(d,".nfo")||
       !strcmp(d,".info"))                              return TypeText;
    return TypeUnknown;
}

static const uint8_t* ffv_icon(FFVFileType t) {
    switch(t){
    case TypeFolder:       return I_dir_10px.frames[0];
    case TypeSubGhz:       return I_sub1_10px.frames[0];
    case TypeSubGhzRemote: return I_subrem_10px.frames[0];
    case TypeNfc:          return I_Nfc_10px.frames[0];
    case TypeInfrared:     return I_ir_10px.frames[0];
    case TypeLFRFID:       return I_125_10px.frames[0];
    case TypeT5577:        return I_125_10px.frames[0];
    case TypeProtoPirate:  return I_sub1_10px.frames[0];
    case TypePicopass:     return I_Nfc_10px.frames[0];
    case TypeText:         return I_file_10px.frames[0];
    case TypeBadUsb:       return I_badusb_10px.frames[0];
    case TypeIButton:      return I_ibutt_10px.frames[0];
    case TypeU2F:          return I_u2f_10px.frames[0];
    case TypeApp:          return I_Apps_10px.frames[0];
    case TypeUpdate:       return I_update_10px.frames[0];
    case TypeScript:       return I_js_script_10px.frames[0];
    case TypeSettings:     return I_settings_10px.frames[0];
    case TypeMusic:        return I_music_10px.frames[0];
    case TypeFile:         return I_file_10px.frames[0];
    case TypeImage:        return I_image_10px.frames[0];
    case TypeBmp:          return I_image_10px.frames[0];
    default:               return I_unknown_10px.frames[0];
    }
}

#define FFV_MAX_APPS_PER_TYPE MENU_MAX_ITEMS
typedef struct {
    FFVFileType type;
    const char* app_names[FFV_MAX_APPS_PER_TYPE];
    uint8_t count;
} FFVAppMapping;

static const FFVAppMapping ffv_app_mapping[] = {
    {TypeSubGhz,       {"Sub-GHz"},         1},
    {TypeSubGhzRemote, {"Sub-GHz Remote"},  1},
    {TypeNfc,          {"NFC"},             1},
    {TypeInfrared,     {"Infrared"},        1},
    {TypeLFRFID,       {"125 kHz RFID"},    1},
    {TypeT5577,        {"T5577"},           1},
    {TypeProtoPirate,  {"ProtoPirate (Fox Edition)"}, 1},
    {TypePicopass,     {"PicoPass"},         1},
    {TypeBadUsb,       {"Bad USB"},         1},
    {TypeIButton,      {"iButton"},         1},
    {TypeU2F,          {"U2F"},             1},
    {TypeScript,       {"JS Runner"},       1},
    {TypeMusic,        {"MP3 Player"},      1},
    {TypeFile,         {"FlipNote"},        1},
};

static uint8_t ffv_apps_for(FFVFileType t, const char* out[FFV_MAX_APPS_PER_TYPE]) {
    for(size_t i = 0; i < COUNT_OF(ffv_app_mapping); i++) {
        if(ffv_app_mapping[i].type == t) {
            for(uint8_t j = 0; j < ffv_app_mapping[i].count; j++) out[j] = ffv_app_mapping[i].app_names[j];
            return ffv_app_mapping[i].count;
        }
    }
    return 0;
}

#define FAV_PATH EXT_PATH("favorites.txt")
#define FAV_TMP  EXT_PATH("favorites.txt.tmp")

#define FOX_XBM_CONVERTER_APP_NAME "Fox XBM Converter"
#define FOX_XBM_CONVERTER_FAP_PATH EXT_PATH("apps/Fox/fox_xbm_converter.fap")

static bool ffv_is_pinned(Storage* st, const char* path) {
    File* f = storage_file_alloc(st);
    bool found = false;
    if(storage_file_open(f, FAV_PATH, FSAM_READ, FSOM_OPEN_EXISTING)) {
        char line[256]; size_t i=0; char c;
        while(storage_file_read(f,&c,1)==1){
            if(c=='\r') continue;
            if(c=='\n'){line[i]='\0';if(!strcmp(line,path)){found=true;break;}i=0;}
            else if(i<sizeof(line)-1) line[i++]=c;
        }

        if(!found && i > 0) { line[i]='\0'; if(!strcmp(line,path)) found=true; }
        storage_file_close(f);
    }
    storage_file_free(f);
    return found;
}
static void ffv_pin(Storage* st, const char* path) {
    if(ffv_is_pinned(st,path)) return;
    File* f=storage_file_alloc(st);
    if(storage_file_open(f,FAV_PATH,FSAM_WRITE,FSOM_OPEN_APPEND)){
        storage_file_write(f,path,strlen(path));
        storage_file_write(f,"\n",1);
        storage_file_close(f);
    }
    storage_file_free(f);
}
static void ffv_unpin(Storage* st, const char* path) {
    File* fi=storage_file_alloc(st); File* fo=storage_file_alloc(st);
    if(storage_file_open(fi,FAV_PATH,FSAM_READ,FSOM_OPEN_EXISTING)){
        if(storage_file_open(fo,FAV_TMP,FSAM_WRITE,FSOM_CREATE_ALWAYS)){
            char line[256]; size_t i=0; char c;
            while(storage_file_read(fi,&c,1)==1){
                if(c=='\r') continue;
                if(c=='\n'){line[i]='\0';if(strcmp(line,path)!=0){storage_file_write(fo,line,i);storage_file_write(fo,"\n",1);}i=0;}
                else if(i<sizeof(line)-1) line[i++]=c;
            }
            storage_file_close(fo);
        }
        storage_file_close(fi);
        storage_common_remove(st,FAV_PATH);
        storage_common_rename(st,FAV_TMP,FAV_PATH);
    }
    storage_file_free(fi); storage_file_free(fo);
}

static void ffv_favs_free(FFVApp* app){
    for(size_t i=0;i<app->fav_count;i++) furi_string_free(app->fav_list[i]);
    free(app->fav_list); app->fav_list=NULL;
    app->fav_count=app->fav_selected=app->fav_scroll=0;
}
static void ffv_favs_load(FFVApp* app){
    ffv_favs_free(app);
    File* f=storage_file_alloc(app->storage);
    if(!storage_file_open(f,FAV_PATH,FSAM_READ,FSOM_OPEN_EXISTING)){storage_file_free(f);return;}
    char line[256]; size_t li=0; char c; size_t cnt=0;
    while(storage_file_read(f,&c,1)==1){if(c=='\r')continue;if(c=='\n'){if(li>0)cnt++;li=0;}else if(li<255)line[li++]=c;}
    if(li>0) cnt++;
    if(!cnt){storage_file_close(f);storage_file_free(f);return;}
    app->fav_list=malloc(cnt*sizeof(FuriString*)); app->fav_count=0;
    if(storage_file_seek(f,0,true)){
        li=0;
        while(storage_file_read(f,&c,1)==1){
            if(c=='\r')continue;
            if(c=='\n'){if(li>0){line[li]='\0';app->fav_list[app->fav_count++]=furi_string_alloc_set_str(line);li=0;}}
            else if(li<255) line[li++]=c;
        }
        if(li>0){line[li]='\0';app->fav_list[app->fav_count++]=furi_string_alloc_set_str(line);}
    }
    storage_file_close(f); storage_file_free(f);
}

static void ffv_draw_row(Canvas* canvas, int row, const char* label, bool sel){
    int ry=ROW_HEADER_H+row*ROW_ITEM_H, by=ry+1, bh=ROW_ITEM_H-2;
    if(sel){canvas_draw_rbox(canvas,2,by,124,bh,3);canvas_set_color(canvas,ColorWhite);}
    else    canvas_draw_rframe(canvas,2,by,124,bh,3);
    canvas_draw_str_aligned(canvas,64,by+bh/2,AlignCenter,AlignCenter,label);
    canvas_set_color(canvas,ColorBlack);
}
static void ffv_draw_scroll(Canvas* canvas, size_t total, size_t vis, size_t scroll){
    if(total<=vis) return;
    int ah=64-ROW_HEADER_H;
    int bh=(int)(ah*vis/total); if(bh<3)bh=3;
    int by=ROW_HEADER_H+(int)(ah*scroll/total);
    canvas_draw_box(canvas,125,by,3,bh);
}

static const char* ffv_start_label(FFVStartItem item){
    switch(item){
    case StartItemBrowse: return "Browse SD Card";
    case StartItemSearch: return "Search SD Card";
    case StartItemFavs:   return "Favorites";
    }
    return "";
}

static void ffv_start_build_items(FFVApp* app){
    app->start_count = 0;
    app->start_items[app->start_count++] = StartItemBrowse;
    app->start_items[app->start_count++] = StartItemSearch;
    if(ffv_favorites_non_empty(app)) app->start_items[app->start_count++] = StartItemFavs;
    if(app->start_selected >= app->start_count) app->start_selected = 0;
}

static void ffv_start_item_rect(uint8_t idx, uint8_t count, int* y, int* h){
    if(count <= 2){
        static const int ys[2] = {18, 41};
        *h = 18; *y = ys[idx];
    } else {
        static const int ys[3] = {14, 31, 48};
        *h = 14; *y = ys[idx];
    }
}

static void ffv_start_draw_cb(Canvas* canvas, void* model){
    UNUSED(model);
    FFVApp* app=s_ffv_ctx; if(!app) return;
    canvas_clear(canvas);

    canvas_set_font(canvas, FontPrimary);
    canvas_draw_str_aligned(canvas, 64, 6, AlignCenter, AlignCenter, "File Browser");

    const int BX = 3, BW = 122, BR = 4;
    for(uint8_t i = 0; i < app->start_count; i++){
        int y, h;
        ffv_start_item_rect(i, app->start_count, &y, &h);
        bool sel = (i == app->start_selected);
        if(sel){
            canvas_draw_rbox(canvas, BX, y, BW, h, BR);
            canvas_set_color(canvas, ColorWhite);
        } else {
            canvas_draw_rframe(canvas, BX, y, BW, h, BR);
        }
        canvas_set_font(canvas, FontPrimary);
        canvas_draw_str_aligned(canvas, 64, y + h/2, AlignCenter, AlignCenter,
                                ffv_start_label(app->start_items[i]));
        canvas_set_color(canvas, ColorBlack);
    }
}
static bool ffv_start_input_cb(InputEvent* ev, void* ctx){
    FFVApp* app=(FFVApp*)ctx;
    if(ev->type!=InputTypeShort && ev->type!=InputTypeRepeat) return false;
    switch(ev->key){
    case InputKeyUp:
        app->start_selected = (app->start_selected==0) ? app->start_count-1 : app->start_selected-1;
        with_view_model(app->start_view,uint8_t* _m,{UNUSED(_m);},true);
        return true;
    case InputKeyDown:
        app->start_selected = (app->start_selected+1)%app->start_count;
        with_view_model(app->start_view,uint8_t* _m,{UNUSED(_m);},true);
        return true;
    case InputKeyOk: case InputKeyRight:
        switch(app->start_items[app->start_selected]){
        case StartItemBrowse: ffv_go_browse(app); break;
        case StartItemSearch: ffv_go_search_input(app); break;
        case StartItemFavs:   ffv_go_favs(app); break;
        }
        return true;
    case InputKeyBack:
        return false;
    default: return false;
    }
}

static void ffb_fav_scroll_timer_cb(void* ctx) {
    FFVApp* app = ctx;
    fox_scroll_text_tick(&app->fav_text_anim);
    with_view_model(app->fav_view, uint8_t* _m, { UNUSED(_m); }, true);
}

static void ffv_ensure_favorites_file(FFVApp* app){
    if(!storage_file_exists(app->storage, FAV_PATH)){
        File* f=storage_file_alloc(app->storage);
        if(storage_file_open(f,FAV_PATH,FSAM_WRITE,FSOM_CREATE_NEW))
            storage_file_close(f);
        storage_file_free(f);
    }
}

static void ffv_fav_scroll_start(FFVApp* app) {
    fox_scroll_text_reset(&app->fav_text_anim);
    furi_timer_start(app->fav_scroll_timer, FAV_SCROLL_MS);
}
static void ffv_fav_scroll_stop(FFVApp* app) {
    furi_timer_stop(app->fav_scroll_timer);
    fox_scroll_text_reset(&app->fav_text_anim);
}

static bool ffv_favorites_non_empty(FFVApp* app){
    File* f=storage_file_alloc(app->storage);
    bool has=false;
    if(storage_file_open(f,FAV_PATH,FSAM_READ,FSOM_OPEN_EXISTING)){
        char c;
        while(storage_file_read(f,&c,1)==1){
            if(c!='\r'&&c!='\n'){has=true;break;}
        }
        storage_file_close(f);
    }
    storage_file_free(f);
    return has;
}

static void ffv_build_path_hint(const char* full_path, char* buf, size_t buf_size){
    if(!buf_size) return;
    const char* p=full_path;
    if(strncmp(p,"/ext/",5)==0) p+=5; else if(*p=='/') p++;

    const char* fn=strrchr(full_path,'/');
    size_t dir_len = (fn && fn>=p) ? (size_t)(fn-p) : 0;
    size_t out=0;

    buf[out++]='/';
    if(out<buf_size-1) buf[out++]=' ';
    for(size_t i=0; i<dir_len && out<buf_size-4; i++){
        if(p[i]=='/'){
            buf[out++]=' '; buf[out++]='/'; buf[out++]=' ';
        } else {
            buf[out++]=p[i];
        }
    }

    if(out+2<=buf_size){ buf[out++]=' '; buf[out++]='/'; }
    buf[out<buf_size?out:buf_size-1]='\0';
}

static bool ffv_stristr_pos(const char* hay, const char* needle, size_t* pos){
    size_t nlen = strlen(needle);
    if(!nlen) return false;
    for(const char* p = hay; *p; p++){
        size_t i = 0;
        while(i < nlen && p[i] && tolower((unsigned char)p[i]) == tolower((unsigned char)needle[i])) i++;
        if(i == nlen){ *pos = (size_t)(p - hay); return true; }
    }
    return false;
}
static bool ffv_stristr(const char* hay, const char* needle){
    size_t pos;
    return ffv_stristr_pos(hay, needle, &pos);
}

static char* ffv_strdup(const char* s){
    size_t n = strlen(s) + 1;
    char* d = malloc(n);
    if(d) memcpy(d, s, n);
    return d;
}

static void ffv_join_path(char* out, size_t out_size, const char* dir, const char* name){
    size_t dlen = strlen(dir);
    if(dlen && dir[dlen-1] == '/') dlen--;
    snprintf(out, out_size, "%.*s/%s", (int)dlen, dir, name);
}

static uint8_t ffv_hex2byte(char hi, char lo) {
    uint8_t h = (hi >= 'a') ? (uint8_t)(hi - 'a' + 10) :
                (hi >= 'A') ? (uint8_t)(hi - 'A' + 10) : (uint8_t)(hi - '0');
    uint8_t l = (lo >= 'a') ? (uint8_t)(lo - 'a' + 10) :
                (lo >= 'A') ? (uint8_t)(lo - 'A' + 10) : (uint8_t)(lo - '0');
    return (h << 4) | l;
}

static bool ffv_parse_xbm(Storage* storage, const char* path, uint8_t* out) {
    File* file = storage_file_alloc(storage);
    if(!storage_file_open(file, path, FSAM_READ, FSOM_OPEN_EXISTING)) {
        storage_file_free(file);
        return false;
    }

    size_t count = 0;
    uint8_t state = 0;
    char hi_digit = 0;
    bool finished = false;

    uint8_t chunk[256];
    uint16_t n;

    while(!finished && count < IMG_BYTES &&
          (n = storage_file_read(file, chunk, sizeof(chunk))) > 0) {
        for(uint16_t i = 0; i < n && count < IMG_BYTES && !finished; i++) {
            char c = (char)chunk[i];
            switch(state) {
            case 0:
                if(c == '{') state = 1;
                break;
            case 1:
                if(c == '}') { finished = true; break; }
                if(c == '0') state = 2;
                break;
            case 2:
                state = (c == 'x' || c == 'X') ? 3 : 1;
                break;
            case 3:
                hi_digit = c;
                state = 4;
                break;
            case 4:
                out[count++] = ffv_hex2byte(hi_digit, c);
                state = 1;
                break;
            }
        }
    }

    storage_file_close(file);
    storage_file_free(file);

    return count == IMG_BYTES;
}

static int ffv_path_cmp(const void* a, const void* b) {
    FuriString* const* sa = a;
    FuriString* const* sb = b;
    return strcmp(furi_string_get_cstr(*sa), furi_string_get_cstr(*sb));
}

static void ffv_image_list_free(FFVApp* app) {
    for(size_t i = 0; i < app->image_count; i++) furi_string_free(app->image_list[i]);
    free(app->image_list);
    app->image_list = NULL;
    app->image_count = 0;
    app->image_index = 0;
}

static void ffv_image_list_build(FFVApp* app, const char* current_path) {
    ffv_image_list_free(app);

    FuriString* dir = furi_string_alloc();
    const char* sl = strrchr(current_path, '/');
    if(sl && sl > current_path) {
        furi_string_set_strn(dir, current_path, (size_t)(sl - current_path));
    } else {
        furi_string_set_str(dir, EXT_PATH(""));
    }

    File* d = storage_file_alloc(app->storage);
    FileInfo info;
    char name[256];
    size_t cap = 0;
    if(storage_dir_open(d, furi_string_get_cstr(dir))) {
        while(storage_dir_read(d, &info, name, sizeof(name))) {
            if(info.flags & FSF_DIRECTORY) continue;
            size_t nlen = strlen(name);
            if(nlen < 5 || strcmp(name + nlen - 4, ".xbm") != 0) continue;
            if(app->image_count == cap) {
                cap = cap ? cap * 2 : 8;
                app->image_list = realloc(app->image_list, cap * sizeof(FuriString*));
            }
            char full[300];
            ffv_join_path(full, sizeof(full), furi_string_get_cstr(dir), name);
            app->image_list[app->image_count++] = furi_string_alloc_set_str(full);
        }
    }
    storage_dir_close(d);
    storage_file_free(d);
    furi_string_free(dir);

    for(size_t i = 1; i < app->image_count; i++) {
        FuriString* key = app->image_list[i];
        size_t j = i;
        while(j > 0 && ffv_path_cmp(&app->image_list[j - 1], &key) > 0) {
            app->image_list[j] = app->image_list[j - 1];
            j--;
        }
        app->image_list[j] = key;
    }

    app->image_index = 0;
    for(size_t i = 0; i < app->image_count; i++) {
        if(!strcmp(furi_string_get_cstr(app->image_list[i]), current_path)) {
            app->image_index = i;
            break;
        }
    }
}

static void ffv_image_load_current(FFVApp* app) {
    app->image_loaded = false;
    if(app->image_count == 0) return;
    const char* path = furi_string_get_cstr(app->image_list[app->image_index]);
    app->image_loaded = ffv_parse_xbm(app->storage, path, app->image_bitmap);
}

static void ffv_do_search(FFVApp* app, const char* needle){
    ffv_favs_free(app);
    if(!needle || !needle[0]) return;

    char** dstack = NULL; size_t dcount = 0, dcap = 0;
    dcap = 8; dstack = malloc(dcap * sizeof(char*));
    dstack[dcount++] = ffv_strdup(EXT_PATH(""));

    size_t list_cap = 0;
    File* dir = storage_file_alloc(app->storage);
    FileInfo info;
    char name[SEARCH_NAME_MAX];

    while(dcount > 0 && app->fav_count < SEARCH_MAX_RESULTS){
        char* cur = dstack[--dcount];
        if(storage_dir_open(dir, cur)){
            while(app->fav_count < SEARCH_MAX_RESULTS &&
                  storage_dir_read(dir, &info, name, sizeof(name))){
                char full[300];
                ffv_join_path(full, sizeof(full), cur, name);
                if(info.flags & FSF_DIRECTORY){
                    if(dcount == dcap){ dcap *= 2; dstack = realloc(dstack, dcap * sizeof(char*)); }
                    dstack[dcount++] = ffv_strdup(full);
                } else if(ffv_stristr(name, needle)){
                    if(app->fav_count == list_cap){
                        list_cap = list_cap ? list_cap * 2 : 16;
                        app->fav_list = realloc(app->fav_list, list_cap * sizeof(FuriString*));
                    }
                    app->fav_list[app->fav_count++] = furi_string_alloc_set_str(full);
                }
            }
        }
        storage_dir_close(dir);
        free(cur);
    }
    storage_file_free(dir);

    while(dcount > 0) free(dstack[--dcount]);
    free(dstack);
}

static void ffv_draw_name_row(Canvas* canvas, int cy, const char* name, const char* query, bool do_underline){
    canvas_set_font(canvas, FontPrimary);
    size_t pos;
    if(!do_underline || !ffv_stristr_pos(name, query, &pos)){
        canvas_draw_str_aligned(canvas, 64, cy, AlignCenter, AlignCenter, name);
        return;
    }
    size_t nlen = strlen(name), qlen = strlen(query);
    size_t mid_len = (pos + qlen > nlen) ? nlen - pos : qlen;

    char pre[SEARCH_NAME_MAX], mid[SEARCH_NAME_MAX], post[SEARCH_NAME_MAX];
    size_t pre_len = pos < sizeof(pre)-1 ? pos : sizeof(pre)-1;
    memcpy(pre, name, pre_len); pre[pre_len] = '\0';
    size_t m_len = mid_len < sizeof(mid)-1 ? mid_len : sizeof(mid)-1;
    memcpy(mid, name + pos, m_len); mid[m_len] = '\0';
    strlcpy(post, name + pos + mid_len, sizeof(post));

    int w_total = (int)canvas_string_width(canvas, name);
    int x = 64 - w_total/2;
    canvas_draw_str_aligned(canvas, x, cy, AlignLeft, AlignCenter, pre);
    x += (int)canvas_string_width(canvas, pre);
    int mid_x = x;
    int mid_w = (int)canvas_string_width(canvas, mid);
    canvas_draw_str_aligned(canvas, mid_x, cy, AlignLeft, AlignCenter, mid);
    canvas_draw_line(canvas, mid_x, cy+6, mid_x+mid_w-1, cy+6);
    x += mid_w;
    canvas_draw_str_aligned(canvas, x, cy, AlignLeft, AlignCenter, post);
}

static void ffv_fav_draw_cb(Canvas* canvas, void* model){
    UNUSED(model);
    FFVApp* app=s_ffv_ctx; if(!app) return;
    canvas_clear(canvas);
    canvas_set_font(canvas,FontPrimary);
    bool is_search = (app->list_source == ListSrcSearch);
    canvas_draw_str_aligned(canvas,64,2,AlignCenter,AlignTop,is_search?"Search Results":"Favorites");
    if(app->fav_count==0){
        canvas_set_font(canvas,FontSecondary);
        const char* msg = app->searching ? "Searching..." : (is_search ? "No matches found" : "");
        canvas_draw_str_aligned(canvas,64,34,AlignCenter,AlignCenter,msg);
        return;
    }

    for(size_t i=app->fav_scroll;
        i<app->fav_count && (int)(i-app->fav_scroll)<FAV_ROW_VIS; i++){
        const char* full=furi_string_get_cstr(app->fav_list[i]);
        const char* sl=strrchr(full,'/');
        const char* name=sl?sl+1:full;
        int row=(int)(i-app->fav_scroll);
        int ry=ROW_HEADER_H+row*FAV_ROW_H;
        int by=ry+1, bh=FAV_ROW_H-2;
        bool sel=(i==app->fav_selected);
        if(sel){ canvas_draw_rbox(canvas,2,by,124,bh,3); canvas_set_color(canvas,ColorWhite); }
        else     canvas_draw_rframe(canvas,2,by,124,bh,3);

        ffv_draw_name_row(canvas, by+5, name, app->search_query, is_search);

        char hint[100];
        ffv_build_path_hint(full,hint,sizeof(hint));
        canvas_set_font(canvas, FontSecondary);
        if(!sel) {
            int inner_w = 116;
            int hw = (int)canvas_string_width(canvas, hint);
            if(hw <= inner_w) {
                canvas_draw_str_aligned(canvas, 64, by+15, AlignCenter, AlignCenter, hint);
            } else {
                int dots_w = (int)canvas_string_width(canvas, "...");
                int avail  = inner_w - dots_w - 2;

                int hlen = (int)strlen(hint);
                int sfx  = 0;
                while(sfx < hlen && (int)canvas_string_width(canvas, hint + sfx) > avail)
                    sfx++;
                char buf[128];
                snprintf(buf, sizeof(buf), "...%s", hint + sfx);
                canvas_draw_str_aligned(canvas, 64, by+15, AlignCenter, AlignCenter, buf);
            }
        } else {
            fox_scroll_text_draw(canvas, 2, by, 124, bh, by+15, 4, true,
                                 hint, &app->fav_text_anim);
        }
        canvas_set_color(canvas,ColorBlack);
    }
    ffv_draw_scroll(canvas,app->fav_count,FAV_ROW_VIS,app->fav_scroll);
}
static bool ffv_fav_input_cb(InputEvent* ev, void* ctx){
    FFVApp* app=(FFVApp*)ctx;
    if(ev->type!=InputTypeShort && ev->type!=InputTypeRepeat) return false;
    if(app->fav_count==0){ return false; }
    switch(ev->key){
    case InputKeyUp:
        fox_scroll_text_reset(&app->fav_text_anim);
        if(app->fav_selected>0){app->fav_selected--;if(app->fav_selected<app->fav_scroll)app->fav_scroll=app->fav_selected;}
        else{app->fav_selected=app->fav_count-1;app->fav_scroll=(app->fav_count>(size_t)FAV_ROW_VIS)?app->fav_count-FAV_ROW_VIS:0;}
        with_view_model(app->fav_view,uint8_t* _m,{UNUSED(_m);},true);
        return true;
    case InputKeyDown:
        fox_scroll_text_reset(&app->fav_text_anim);
        if(app->fav_selected+1<app->fav_count){app->fav_selected++;if(app->fav_selected>=app->fav_scroll+FAV_ROW_VIS)app->fav_scroll=app->fav_selected-FAV_ROW_VIS+1;}
        else{app->fav_selected=0;app->fav_scroll=0;}
        with_view_model(app->fav_view,uint8_t* _m,{UNUSED(_m);},true);
        return true;
    case InputKeyOk: case InputKeyRight:
        ffv_fav_scroll_stop(app);
        furi_string_set(app->selected_path,app->fav_list[app->fav_selected]);
        app->menu_from_favs=true;
        ffv_build_menu(app);
        app->current_view=ViewMenu;
        view_dispatcher_switch_to_view(app->view_dispatcher,ViewMenu);
        return true;
    case InputKeyBack: case InputKeyLeft:
        return false;
    default: return false;
    }
}

static void ffv_menu_draw_cb(Canvas* canvas, void* model){
    UNUSED(model);
    FFVApp* app=s_ffv_ctx; if(!app) return;
    canvas_clear(canvas);
    if(app->menu_toast_active){
        canvas_set_font(canvas,FontSecondary);
        canvas_draw_str_aligned(canvas,64,26,AlignCenter,AlignCenter,"Convert to XBM to view");
        canvas_draw_str_aligned(canvas,64,38,AlignCenter,AlignCenter,"on Flipper Zero");
        return;
    }
    canvas_set_font(canvas,FontPrimary);
    canvas_draw_str_aligned(canvas,64,2,AlignCenter,AlignTop,
                             app->menu_is_app_picker?"Open With":"File Options");
    canvas_set_font(canvas,FontSecondary);
    for(uint8_t i=0;i<app->menu_count;i++)
        ffv_draw_row(canvas,i,app->menu_labels[i],i==app->menu_selected);
}
static bool ffv_menu_input_cb(InputEvent* ev, void* ctx){
    FFVApp* app=(FFVApp*)ctx;
    if(app->menu_toast_active) return true;
    if(ev->type!=InputTypeShort && ev->type!=InputTypeRepeat) return false;
    switch(ev->key){
    case InputKeyUp:
        app->menu_selected=(app->menu_selected==0)?app->menu_count-1:app->menu_selected-1;
        with_view_model(app->menu_view,uint8_t* _m,{UNUSED(_m);},true);
        return true;
    case InputKeyDown:
        app->menu_selected=(app->menu_selected+1)%app->menu_count;
        with_view_model(app->menu_view,uint8_t* _m,{UNUSED(_m);},true);
        return true;
    case InputKeyOk: case InputKeyRight:
        ffv_do_action(app,app->menu_actions[app->menu_selected]);
        return true;
    case InputKeyBack: case InputKeyLeft:
        return false;
    default: return false;
    }
}
static void ffv_menu_toast_timer_cb(void* ctx){
    FFVApp* app=(FFVApp*)ctx;
    app->menu_toast_active=false;
    with_view_model(app->menu_view,uint8_t* _m,{UNUSED(_m);},true);
}
static void ffv_show_menu_toast(FFVApp* app){
    app->menu_toast_active=true;
    furi_timer_start(app->menu_toast_timer, MENU_TOAST_MS);
    with_view_model(app->menu_view,uint8_t* _m,{UNUSED(_m);},true);
}

static void ffv_image_draw_cb(Canvas* canvas, void* model){
    UNUSED(model);
    FFVApp* app=s_ffv_ctx; if(!app) return;
    canvas_clear(canvas);
    if(app->image_loaded){
        canvas_draw_xbm(canvas, 0, 0, IMG_W, IMG_H, app->image_bitmap);
    } else {
        canvas_set_font(canvas, FontSecondary);
        canvas_draw_str_aligned(canvas, 64, 32, AlignCenter, AlignCenter, "Can't open image");
    }
}
static bool ffv_image_input_cb(InputEvent* ev, void* ctx){
    FFVApp* app=(FFVApp*)ctx;
    if(ev->type!=InputTypeShort) return false;
    if(app->image_count==0) return false;
    switch(ev->key){
    case InputKeyLeft:
        app->image_index = (app->image_index==0) ? app->image_count-1 : app->image_index-1;
        ffv_image_load_current(app);
        with_view_model(app->image_view,uint8_t* _m,{UNUSED(_m);},true);
        return true;
    case InputKeyRight:
        app->image_index = (app->image_index+1)%app->image_count;
        ffv_image_load_current(app);
        with_view_model(app->image_view,uint8_t* _m,{UNUSED(_m);},true);
        return true;
    case InputKeyBack:
        return false;
    default: return false;
    }
}

static void ffv_go_image_view(FFVApp* app){
    const char* path = furi_string_get_cstr(app->selected_path);
    ffv_image_list_build(app, path);
    ffv_image_load_current(app);
    app->image_from_favs = app->menu_from_favs;
    app->current_view = ViewImage;
    view_dispatcher_switch_to_view(app->view_dispatcher, ViewImage);
}

static void ffv_text_free(FFVApp* app){
    if(app->text_buf){ free(app->text_buf); app->text_buf=NULL; }
    if(app->text_lines){ free(app->text_lines); app->text_lines=NULL; }
    app->text_len=0; app->text_line_count=0; app->text_top=0;
}

static void ffv_text_load(FFVApp* app, const char* path){
    ffv_text_free(app);
    app->text_lines = malloc(sizeof(size_t) * TXT_MAX_LINES);
    if(!app->text_lines) return;
    app->text_buf = malloc(TXT_CAP + 1);
    if(!app->text_buf){ free(app->text_lines); app->text_lines=NULL; return; }

    File* file = storage_file_alloc(app->storage);
    size_t rd = 0;
    if(storage_file_open(file, path, FSAM_READ, FSOM_OPEN_EXISTING)){
        while(rd < TXT_CAP){
            size_t got = storage_file_read(file, app->text_buf + rd, TXT_CAP - rd);
            if(got == 0) break;
            rd += got;
        }
    }
    storage_file_close(file);
    storage_file_free(file);
    app->text_buf[rd] = '\0';

    /* normalise in place: drop CR, TAB -> space, other control bytes -> '.' so a
       stray binary byte cannot corrupt the glyph run; keep LF as the line break */
    size_t w = 0;
    for(size_t r = 0; r < rd; r++){
        unsigned char c = (unsigned char)app->text_buf[r];
        if(c == '\r') continue;
        else if(c == '\t') app->text_buf[w++] = ' ';
        else if(c == '\n') app->text_buf[w++] = '\n';
        else if(c < 0x20 || c == 0x7f) app->text_buf[w++] = '.';
        else app->text_buf[w++] = (char)c;
    }
    app->text_buf[w] = '\0';
    app->text_len = w;

    /* wrapped line-start index: break on LF or every TXT_WRAP_COLS chars */
    size_t lc = 0;
    app->text_lines[lc++] = 0;
    size_t col = 0;
    for(size_t i = 0; i < w && lc < TXT_MAX_LINES; i++){
        if(app->text_buf[i] == '\n'){
            app->text_lines[lc++] = i + 1;
            col = 0;
        } else {
            col++;
            if(col >= TXT_WRAP_COLS && (i + 1) < w && app->text_buf[i+1] != '\n'){
                app->text_lines[lc++] = i + 1;
                col = 0;
            }
        }
    }
    app->text_line_count = lc;
    app->text_top = 0;
}

static void ffv_text_draw_cb(Canvas* canvas, void* model){
    UNUSED(model);
    FFVApp* app = s_ffv_ctx; if(!app) return;
    canvas_clear(canvas);
    canvas_set_font(canvas, FontSecondary);

    if(!app->text_buf || app->text_line_count == 0){
        canvas_draw_str_aligned(canvas, 64, 32, AlignCenter, AlignCenter,
            app->text_buf ? "(empty)" : "Can't open file");
        return;
    }

    char line[TXT_WRAP_COLS + 1];
    int y = 10;
    for(size_t row = 0; row < TXT_ROWS_VIS; row++){
        size_t li = app->text_top + row;
        if(li >= app->text_line_count) break;
        size_t start = app->text_lines[li];
        size_t end = (li + 1 < app->text_line_count) ? app->text_lines[li+1] : app->text_len;
        if(end > start && app->text_buf[end-1] == '\n') end--;
        size_t n = end - start;
        if(n > TXT_WRAP_COLS) n = TXT_WRAP_COLS;
        memcpy(line, app->text_buf + start, n);
        line[n] = '\0';
        canvas_draw_str(canvas, 0, y, line);
        y += 10;
    }

    if(app->text_line_count > TXT_ROWS_VIS){
        size_t max_top = app->text_line_count - TXT_ROWS_VIS;
        int track = 64;
        int knob = (int)((uint32_t)app->text_top * (track - 6) / (max_top ? max_top : 1));
        canvas_draw_frame(canvas, 125, 0, 3, track);
        canvas_draw_box(canvas, 125, knob, 3, 6);
    }
}

static bool ffv_text_input_cb(InputEvent* ev, void* ctx){
    FFVApp* app = (FFVApp*)ctx;
    if(ev->key == InputKeyBack) return false;
    if(ev->type != InputTypeShort && ev->type != InputTypeRepeat) return false;
    if(app->text_line_count == 0) return false;
    size_t max_top = (app->text_line_count > TXT_ROWS_VIS) ? app->text_line_count - TXT_ROWS_VIS : 0;
    switch(ev->key){
    case InputKeyUp:
        if(app->text_top > 0) app->text_top--;
        break;
    case InputKeyDown:
        if(app->text_top < max_top) app->text_top++;
        break;
    case InputKeyLeft:
        app->text_top = (app->text_top > TXT_ROWS_VIS) ? app->text_top - TXT_ROWS_VIS : 0;
        break;
    case InputKeyRight:
        app->text_top = (app->text_top + TXT_ROWS_VIS < max_top) ? app->text_top + TXT_ROWS_VIS : max_top;
        break;
    default:
        return false;
    }
    with_view_model(app->text_view, uint8_t* _m, { UNUSED(_m); }, true);
    return true;
}

static void ffv_go_text_view(FFVApp* app){
    const char* path = furi_string_get_cstr(app->selected_path);
    ffv_text_load(app, path);
    app->text_from_favs = app->menu_from_favs;
    app->current_view = ViewText;
    view_dispatcher_switch_to_view(app->view_dispatcher, ViewText);
}

static bool ffv_nav_callback(void* ctx){
    FFVApp* app=(FFVApp*)ctx;
    switch(app->current_view){
    case ViewImage: {

        if(app->image_count > 0) {
            furi_string_set(app->selected_path, app->image_list[app->image_index]);
        }
        ffv_image_list_free(app);
        if(app->image_from_favs) {
            const char* cur = furi_string_get_cstr(app->selected_path);
            for(size_t i = 0; i < app->fav_count; i++) {
                if(!strcmp(furi_string_get_cstr(app->fav_list[i]), cur)) {
                    app->fav_selected = i;
                    if(app->fav_selected < app->fav_scroll) {
                        app->fav_scroll = app->fav_selected;
                    } else if(app->fav_selected >= app->fav_scroll + FAV_ROW_VIS) {
                        app->fav_scroll = app->fav_selected - FAV_ROW_VIS + 1;
                    }
                    break;
                }
            }

            ffv_fav_scroll_start(app);
            app->current_view = ViewFavourites;
            view_dispatcher_switch_to_view(app->view_dispatcher, ViewFavourites);
        } else {
            file_browser_stop(app->browser);
            file_browser_start(app->browser, app->selected_path);
            app->current_view = ViewBrowser;
            view_dispatcher_switch_to_view(app->view_dispatcher, ViewBrowser);
        }
        return true;
    }
    case ViewText: {
        ffv_text_free(app);
        if(app->text_from_favs) {
            ffv_fav_scroll_start(app);
            app->current_view = ViewFavourites;
            view_dispatcher_switch_to_view(app->view_dispatcher, ViewFavourites);
        } else {
            file_browser_stop(app->browser);
            file_browser_start(app->browser, app->selected_path);
            app->current_view = ViewBrowser;
            view_dispatcher_switch_to_view(app->view_dispatcher, ViewBrowser);
        }
        return true;
    }
    case ViewMenu:
        if(app->menu_is_app_picker){

            ffv_build_menu(app);
            with_view_model(app->menu_view,uint8_t* _m,{UNUSED(_m);},true);
        } else {
            ffv_return_from_menu(app);
        }
        return true;
    case ViewTextInput:
        if(app->text_input_mode == TextInputSearch) ffv_go_start(app);
        else                                          ffv_return_from_menu(app);
        return true;
    case ViewBrowser:

        ffv_go_start(app);
        return true;
    case ViewFavourites:

        ffv_fav_scroll_stop(app);
        if(app->list_source == ListSrcSearch) ffv_go_search_input(app);
        else                                   ffv_go_start(app);
        return true;
    case ViewStart:

        return false;
    }
    return false;
}

static void ffv_go_start(FFVApp* app){
    ffv_start_build_items(app);
    app->current_view=ViewStart;
    view_dispatcher_switch_to_view(app->view_dispatcher,ViewStart);
}
static void ffv_go_browse(FFVApp* app){
    file_browser_start(app->browser, app->current_dir);
    app->browser_started = true;
    app->current_view=ViewBrowser;
    view_dispatcher_switch_to_view(app->view_dispatcher,ViewBrowser);
}
static void ffv_go_favs(FFVApp* app){
    ffv_favs_load(app);
    app->list_source = ListSrcFavorites;
    if(app->fav_count==0){
        ffv_favs_free(app);
        ffv_go_browse(app);
        return;
    }
    app->fav_selected=0; app->fav_scroll=0;
    ffv_fav_scroll_start(app);
    app->current_view=ViewFavourites;
    view_dispatcher_switch_to_view(app->view_dispatcher,ViewFavourites);
}
static void ffv_go_search_input(FFVApp* app){
    text_input_reset(app->text_input);
    text_input_set_result_callback(app->text_input, ffv_search_done, app,
                                   app->search_query, sizeof(app->search_query), true);
    text_input_set_header_text(app->text_input, "Search SD Card:");
    app->text_input_mode = TextInputSearch;
    app->current_view = ViewTextInput;
    view_dispatcher_switch_to_view(app->view_dispatcher, ViewTextInput);
}

static void ffv_refresh_browser(FFVApp* app){
    const char* sel=furi_string_get_cstr(app->selected_path);
    const char* sl=strrchr(sel,'/');
    if(sl && sl>sel){ furi_string_set_str(app->current_dir,sel); furi_string_left(app->current_dir,(size_t)(sl-sel)); }
    else furi_string_set_str(app->current_dir,EXT_PATH(""));
    file_browser_stop(app->browser);
    file_browser_start(app->browser,app->current_dir);
    app->current_view=ViewBrowser;
    view_dispatcher_switch_to_view(app->view_dispatcher,ViewBrowser);
}
static void ffv_return_from_menu(FFVApp* app){
    if(app->menu_from_favs && app->list_source == ListSrcSearch){
        ffv_do_search(app, app->search_query);
        if(app->fav_selected>=app->fav_count) app->fav_selected = app->fav_count ? app->fav_count-1 : 0;
        if(app->fav_scroll>app->fav_selected) app->fav_scroll = app->fav_selected;
        ffv_fav_scroll_start(app);
        app->current_view=ViewFavourites;
        view_dispatcher_switch_to_view(app->view_dispatcher,ViewFavourites);
    } else if(app->menu_from_favs){
        ffv_favs_load(app);
        if(app->fav_count==0){
            ffv_favs_free(app);
            ffv_go_browse(app);
        } else {
            if(app->fav_selected>=app->fav_count) app->fav_selected=app->fav_count-1;
            ffv_fav_scroll_start(app);
            app->current_view=ViewFavourites;
            view_dispatcher_switch_to_view(app->view_dispatcher,ViewFavourites);
        }
    } else {
        ffv_refresh_browser(app);
    }
}

static bool ffv_item_cb(FuriString* path, void* ctx, uint8_t** icon, FuriString* name){
    UNUSED(ctx); UNUSED(name);
    *icon=(uint8_t*)ffv_icon(ffv_type_from_path(furi_string_get_cstr(path)));
    return true;
}

static void ffv_build_menu(FFVApp* app){
    const char* path=furi_string_get_cstr(app->selected_path);
    FFVFileType t=ffv_type_from_path(path);
    app->menu_count=app->menu_selected=0;
    app->menu_is_app_picker=false;
    const char* run_candidates[FFV_MAX_APPS_PER_TYPE];
    bool has_converter = storage_file_exists(app->storage, FOX_XBM_CONVERTER_FAP_PATH);

    bool run=(t==TypeUpdate||t==TypeApp||t==TypeImage||t==TypeBmp||
              ffv_apps_for(t,run_candidates)>0);
    if(run){
        const char* label = (t==TypeUpdate)                                ? "Install"   :
                             ((t==TypeImage||t==TypeBmp) && !has_converter) ? "View"      : "Run in app";
        strlcpy(app->menu_labels[app->menu_count], label, 24);
        app->menu_actions[app->menu_count++]=MenuRun;
    }
    bool pin=ffv_is_pinned(app->storage,path);
    strlcpy(app->menu_labels[app->menu_count],pin?"Remove Favorite":"Add to Favorites",24);app->menu_actions[app->menu_count++]=MenuPin;
    strlcpy(app->menu_labels[app->menu_count],"Rename",24);app->menu_actions[app->menu_count++]=MenuRename;
    strlcpy(app->menu_labels[app->menu_count],"Delete",24);app->menu_actions[app->menu_count++]=MenuDelete;
}

static void ffv_browser_callback(void* ctx){
    FFVApp* app=ctx;
    app->menu_from_favs=false;
    ffv_build_menu(app);
    app->current_view=ViewMenu;
    view_dispatcher_switch_to_view(app->view_dispatcher,ViewMenu);
}

static void ffv_rename_done(void* ctx){
    FFVApp* app=ctx;
    const char* old=furi_string_get_cstr(app->selected_path);
    if(!strlen(app->rename_buf)){ffv_return_from_menu(app);return;}
    FuriString* np=furi_string_alloc_set(app->selected_path);
    size_t sl=furi_string_search_rchar(np,'/');
    if(sl!=FURI_STRING_FAILURE){furi_string_left(np,sl+1);furi_string_cat_str(np,app->rename_buf);}
    if(ffv_is_pinned(app->storage,old)){ffv_unpin(app->storage,old);ffv_pin(app->storage,furi_string_get_cstr(np));}
    storage_common_rename(app->storage,old,furi_string_get_cstr(np));
    furi_string_free(np);
    ffv_return_from_menu(app);
}

static void ffv_search_done(void* ctx){
    FFVApp* app=ctx;
    if(!strlen(app->search_query)){ ffv_go_start(app); return; }

    ffv_favs_free(app);
    app->list_source = ListSrcSearch;
    app->fav_selected = app->fav_scroll = 0;
    app->searching = true;
    app->current_view = ViewFavourites;
    view_dispatcher_switch_to_view(app->view_dispatcher, ViewFavourites);

    ffv_do_search(app, app->search_query);

    app->searching = false;
    ffv_fav_scroll_start(app);
    with_view_model(app->fav_view, uint8_t* _m, { UNUSED(_m); }, true);
}

static void ffv_do_action(FFVApp* app, FFVMenuAction action){
    const char* path=furi_string_get_cstr(app->selected_path);
    switch(action){
    case MenuRun: {
        if(app->menu_is_app_picker) {

            if(app->app_picker_targets[app->menu_selected][0] == '\0') {
                FFVFileType vt = ffv_type_from_path(path);
                if(vt == TypeBmp) {
                    ffv_show_menu_toast(app);
                    break;
                }
                app->menu_is_app_picker = false;
                if(vt == TypeText || vt == TypeFile) {
                    ffv_go_text_view(app);
                } else {
                    ffv_go_image_view(app);
                }
                break;
            }
            app->menu_is_app_picker = false;
            loader_enqueue_launch(
                app->loader,
                app->app_picker_targets[app->menu_selected],
                path,
                LoaderDeferredLaunchFlagGui);
            view_dispatcher_stop(app->view_dispatcher);
            break;
        }

        FFVFileType t = ffv_type_from_path(path);
        char fap_path_buf[512];

        if(t == TypeUpdate) {
            loader_enqueue_launch(
                app->loader, "updater_app", path, LoaderDeferredLaunchFlagGui);
            view_dispatcher_stop(app->view_dispatcher);
        } else if(t == TypeApp) {
            strlcpy(fap_path_buf, path, sizeof(fap_path_buf));
            loader_enqueue_launch(
                app->loader, fap_path_buf, NULL, LoaderDeferredLaunchFlagGui);
            view_dispatcher_stop(app->view_dispatcher);
        } else if(t == TypeImage || t == TypeBmp) {

            if(!storage_file_exists(app->storage, FOX_XBM_CONVERTER_FAP_PATH)) {
                if(t == TypeBmp) {
                    ffv_show_menu_toast(app);
                } else {
                    ffv_go_image_view(app);
                }
            } else {
                app->menu_count = 0;
                strlcpy(app->menu_labels[app->menu_count], "View", 24);
                app->app_picker_targets[app->menu_count][0] = '\0';
                app->menu_actions[app->menu_count] = MenuRun;
                app->menu_count++;
                strlcpy(app->menu_labels[app->menu_count], FOX_XBM_CONVERTER_APP_NAME, 24);

                strlcpy(app->app_picker_targets[app->menu_count], FOX_XBM_CONVERTER_FAP_PATH, 48);
                app->menu_actions[app->menu_count] = MenuRun;
                app->menu_count++;
                app->menu_selected = 0;
                app->menu_is_app_picker = true;
                with_view_model(app->menu_view, uint8_t* _m, { UNUSED(_m); }, true);
            }
        } else if(t == TypeText) {
            ffv_go_text_view(app);
        } else if(t == TypeFile) {
            app->menu_count = 0;
            strlcpy(app->menu_labels[app->menu_count], "View", 24);
            app->app_picker_targets[app->menu_count][0] = '\0';
            app->menu_actions[app->menu_count] = MenuRun;
            app->menu_count++;
            strlcpy(app->menu_labels[app->menu_count], "FlipNote", 24);
            strlcpy(app->app_picker_targets[app->menu_count], "FlipNote", 24);
            app->menu_actions[app->menu_count] = MenuRun;
            app->menu_count++;
            app->menu_selected = 0;
            app->menu_is_app_picker = true;
            with_view_model(app->menu_view, uint8_t* _m, { UNUSED(_m); }, true);
        } else {
            const char* candidates[FFV_MAX_APPS_PER_TYPE];
            uint8_t n = ffv_apps_for(t, candidates);
            if(n == 1) {
                loader_enqueue_launch(
                    app->loader, candidates[0], path, LoaderDeferredLaunchFlagGui);
                view_dispatcher_stop(app->view_dispatcher);
            } else if(n > 1) {

                app->menu_count = 0;
                for(uint8_t i = 0; i < n; i++) {
                    strlcpy(app->menu_labels[app->menu_count], candidates[i], 24);
                    strlcpy(app->app_picker_targets[app->menu_count], candidates[i], 24);
                    app->menu_actions[app->menu_count] = MenuRun;
                    app->menu_count++;
                }
                app->menu_selected = 0;
                app->menu_is_app_picker = true;
                with_view_model(app->menu_view, uint8_t* _m, { UNUSED(_m); }, true);
            } else {

                view_dispatcher_stop(app->view_dispatcher);
            }
        }
        break;
    }

    case MenuPin: {
        bool was = ffv_is_pinned(app->storage, path);
        if(was) ffv_unpin(app->storage, path);
        else    ffv_pin(app->storage, path);

        if(app->menu_from_favs && app->list_source == ListSrcFavorites) {
            ffv_return_from_menu(app);
        } else {
            for(uint8_t i = 0; i < app->menu_count; i++) {
                if(app->menu_actions[i] == MenuPin) {
                    strlcpy(app->menu_labels[i],
                            was ? "Add to Favorites" : "Remove Favorite", 24);
                    break;
                }
            }
            with_view_model(app->menu_view, uint8_t* _m, { UNUSED(_m); }, true);
        }
        break;
    }

    case MenuRename: {
        const char* sl=strrchr(path,'/');
        strlcpy(app->rename_buf,sl?sl+1:path,sizeof(app->rename_buf));
        text_input_reset(app->text_input);
        text_input_set_result_callback(app->text_input, ffv_rename_done, app,
                                       app->rename_buf, sizeof(app->rename_buf), true);
        text_input_set_header_text(app->text_input,"Rename:");
        app->text_input_mode=TextInputRename;
        app->current_view=ViewTextInput;
        view_dispatcher_switch_to_view(app->view_dispatcher,ViewTextInput);
        break;
    }

    case MenuDelete: {
        DialogMessage* m=dialog_message_alloc();
        dialog_message_set_header(m,"Delete file?",64,4,AlignCenter,AlignTop);
        const char* sl=strrchr(path,'/');
        dialog_message_set_text(m,sl?sl+1:path,64,32,AlignCenter,AlignCenter);
        dialog_message_set_buttons(m,"Cancel",NULL,"Delete");
        DialogMessageButton btn=dialog_message_show(app->dialogs,m);
        dialog_message_free(m);
        if(btn==DialogMessageButtonRight){
            if(ffv_is_pinned(app->storage,path)) ffv_unpin(app->storage,path);
            storage_common_remove(app->storage,path);
        }
        ffv_return_from_menu(app);
        break;
    }
    }
}

static FFVApp* ffv_alloc(void){
    FFVApp* app=malloc(sizeof(FFVApp)); furi_assert(app);
    memset(app,0,sizeof(FFVApp));
    app->storage =furi_record_open(RECORD_STORAGE);
    app->loader  =furi_record_open(RECORD_LOADER);
    app->dialogs =furi_record_open(RECORD_DIALOGS);
    app->gui     =furi_record_open(RECORD_GUI);
    app->current_view=ViewStart;
    s_ffv_ctx=app;
    app->selected_path=furi_string_alloc();
    app->current_dir  =furi_string_alloc();
    furi_string_set_str(app->current_dir,EXT_PATH(""));

    app->view_dispatcher=view_dispatcher_alloc();
    view_dispatcher_attach_to_gui(app->view_dispatcher,app->gui,ViewDispatcherTypeFullscreen);
    view_dispatcher_set_event_callback_context(app->view_dispatcher,app);
    view_dispatcher_set_navigation_event_callback(app->view_dispatcher,ffv_nav_callback);

    app->start_view=view_alloc();
    view_set_draw_callback(app->start_view,ffv_start_draw_cb);
    view_set_input_callback(app->start_view,ffv_start_input_cb);
    view_set_context(app->start_view,app);
    view_allocate_model(app->start_view,ViewModelTypeLocking,sizeof(uint8_t));
    view_dispatcher_add_view(app->view_dispatcher,ViewStart,app->start_view);

    app->browser=file_browser_alloc(app->selected_path);
    file_browser_configure(app->browser,"*",EXT_PATH(""),false,true,&I_unknown_10px,false);
    file_browser_set_item_callback(app->browser,ffv_item_cb,app);
    file_browser_set_callback(app->browser,ffv_browser_callback,app);
    view_dispatcher_add_view(app->view_dispatcher,ViewBrowser,file_browser_get_view(app->browser));

    app->fav_view=view_alloc();
    view_set_draw_callback(app->fav_view,ffv_fav_draw_cb);
    view_set_input_callback(app->fav_view,ffv_fav_input_cb);
    view_set_context(app->fav_view,app);
    view_allocate_model(app->fav_view,ViewModelTypeLocking,sizeof(uint8_t));
    app->fav_scroll_timer=furi_timer_alloc(ffb_fav_scroll_timer_cb,FuriTimerTypePeriodic,app);
    view_dispatcher_add_view(app->view_dispatcher,ViewFavourites,app->fav_view);

    app->menu_view=view_alloc();
    view_set_draw_callback(app->menu_view,ffv_menu_draw_cb);
    view_set_input_callback(app->menu_view,ffv_menu_input_cb);
    view_set_context(app->menu_view,app);
    view_allocate_model(app->menu_view,ViewModelTypeLocking,sizeof(uint8_t));
    app->menu_toast_timer=furi_timer_alloc(ffv_menu_toast_timer_cb,FuriTimerTypeOnce,app);
    view_dispatcher_add_view(app->view_dispatcher,ViewMenu,app->menu_view);

    app->text_input=text_input_alloc();
    view_dispatcher_add_view(app->view_dispatcher,ViewTextInput,text_input_get_view(app->text_input));

    app->image_view=view_alloc();
    view_set_draw_callback(app->image_view,ffv_image_draw_cb);
    view_set_input_callback(app->image_view,ffv_image_input_cb);
    view_set_context(app->image_view,app);
    view_allocate_model(app->image_view,ViewModelTypeLocking,sizeof(uint8_t));
    view_dispatcher_add_view(app->view_dispatcher,ViewImage,app->image_view);

    app->text_view=view_alloc();
    view_set_draw_callback(app->text_view,ffv_text_draw_cb);
    view_set_input_callback(app->text_view,ffv_text_input_cb);
    view_set_context(app->text_view,app);
    view_allocate_model(app->text_view,ViewModelTypeLocking,sizeof(uint8_t));
    view_dispatcher_add_view(app->view_dispatcher,ViewText,app->text_view);

    return app;
}

static void ffv_free(FFVApp* app){
    s_ffv_ctx=NULL;
    ffv_favs_free(app);
    ffv_image_list_free(app);
    ffv_text_free(app);

    if(app->browser_started) file_browser_stop(app->browser);
    view_dispatcher_remove_view(app->view_dispatcher,ViewStart);
    view_dispatcher_remove_view(app->view_dispatcher,ViewBrowser);
    view_dispatcher_remove_view(app->view_dispatcher,ViewFavourites);
    view_dispatcher_remove_view(app->view_dispatcher,ViewMenu);
    view_dispatcher_remove_view(app->view_dispatcher,ViewTextInput);
    view_dispatcher_remove_view(app->view_dispatcher,ViewImage);
    view_dispatcher_remove_view(app->view_dispatcher,ViewText);
    view_free(app->start_view);
    file_browser_free(app->browser);
    furi_timer_stop(app->fav_scroll_timer);
    furi_timer_free(app->fav_scroll_timer);
    furi_timer_stop(app->menu_toast_timer);
    furi_timer_free(app->menu_toast_timer);
    view_free(app->fav_view);
    view_free(app->menu_view);
    text_input_free(app->text_input);
    view_free(app->image_view);
    view_free(app->text_view);
    view_dispatcher_free(app->view_dispatcher);
    furi_string_free(app->selected_path);
    furi_string_free(app->current_dir);
    furi_record_close(RECORD_GUI);
    furi_record_close(RECORD_STORAGE);
    furi_record_close(RECORD_LOADER);
    furi_record_close(RECORD_DIALOGS);
    free(app);
}

int32_t ffb_app(void* p){
    UNUSED(p);
    FFVApp* app=ffv_alloc();

    ffv_ensure_favorites_file(app);

    ffv_go_start(app);
    view_dispatcher_run(app->view_dispatcher);

    ffv_free(app);
    return 0;
}
