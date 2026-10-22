// 生成物。編集しない（export_game_api.py が作り直す）。
// build: acnl_jpn_v15
// code SHA256: c635af8f08937b66f123b91b02391a68c034393320cdb94d603b9291383faabb
#pragma once
#include <cstdint>

namespace gohan::game::generated {

struct NwFontCharWidths;
struct NwFontCmap;
struct NwFontCmapScanPair;
struct NwFontCmapScanPrefix;
struct NwFontCwdh;
struct NwFontTglp;
struct NwFontFinf;
struct NwFontResFontVtable;
struct NwFontResFont;
struct NwFontGlyphTextureDescriptor;
struct NwFontGlyph;
struct NwFontTagProcessorVtable;
struct NwFontTagProcessor;
struct NwFontTextBatchGlyph;
struct NwFontTextBatchPrefix;
struct NwLytAlphaCompare;
struct NwLytAnimTransform;
struct NwLytPaneLink;
struct NwLytAnimationLink;
struct NwLytAnimationLinkList;
struct NwLytBlendMode;
struct NwLytDrawContext;
struct NwLytDrawInfoVtable;
struct NwLytTextWork;
struct NwLytDrawInfo;
struct NwLytDrawer;
struct NwLytMaterialVtable;
struct NwLytTexMap;
struct NwLytMaterial;
struct NwLytPaneVtable;
struct NwLytPane;
struct NwLytTexCoordQuad;
struct NwLytPicture;
struct NwLytPictureVtable;
struct NwLytTevStage;
struct NwLytTexCoordGen;
struct NwLytTexSrt;
struct NwLytTextBox;
struct NwLytTextBoxVtable;
struct SeadExpHeapBlock;
struct SeadExpHeapList;
struct SeadExpHeapVtable;
struct SeadExpHeapPrefix;
struct SsysMaAllocatorVtable;
struct SsysMaAllocator;
struct SsysMaLayoutNodeVtable;
struct SsysMaLayoutNode;
struct SsysMaLayoutList;
struct SsysMaLayoutMgr;

#pragma pack(push, 1)

struct NwFontCharWidths
{
  std::int8_t left;                   
  std::uint8_t glyph_width;
  std::int8_t char_width;
};
#pragma pack(pop)

#pragma pack(push, 1)

struct NwFontCmap
{
  std::uint16_t first_character;     
  std::uint16_t last_character;
  std::uint16_t method;
  std::uint8_t unk_6[2];
  NwFontCmap *next;
};
#pragma pack(pop)

#pragma pack(push, 1)

struct NwFontCmapScanPair
{
  std::uint16_t character;
  std::uint16_t glyph;
};
#pragma pack(pop)

#pragma pack(push, 1)

struct NwFontCmapScanPrefix
{
  NwFontCmap cmap;
  std::uint16_t pair_count;
  NwFontCmapScanPair pairs[];
};
#pragma pack(pop)

#pragma pack(push, 1)


struct NwFontCwdh
{
  std::uint16_t first_glyph;         
  std::uint16_t last_glyph;
  NwFontCwdh *next;
  NwFontCharWidths widths[];
};
#pragma pack(pop)

#pragma pack(push, 1)

struct NwFontTglp
{
  std::uint8_t cell_width;           
  std::uint8_t cell_height;
  std::int8_t baseline;
  std::uint8_t max_char_width;
  unsigned int sheet_bytes;
  std::uint16_t sheet_count;
  std::uint16_t sheet_format;
  std::uint16_t sheet_columns;
  std::uint16_t sheet_rows;
  std::uint16_t sheet_width;
  std::uint16_t sheet_height;
  void *sheet_data;
};
#pragma pack(pop)

#pragma pack(push, 1)

struct NwFontFinf
{
  std::uint8_t unk_0[1];             
  std::int8_t line_feed;
  std::uint16_t alternate_glyph;
  NwFontCharWidths default_widths;
  std::uint8_t encoding;
  NwFontTglp *tglp;
  NwFontCwdh *cwdh;
  NwFontCmap *cmap;
  std::uint8_t height;
  std::uint8_t width;
  std::uint8_t ascent;
  std::uint8_t unk_17[1];
};
#pragma pack(pop)

#pragma pack(push, 1)

struct NwFontResFontVtable
{
  void *unk_0;                          
  void *unk_4;
  int ( *get_width)(NwFontResFont *font);
  int ( *get_height)(NwFontResFont *font);
  void *unk_10;
  void *unk_14;
  void *unk_18;
  void *unk_1C;
  void *unk_20;
  int ( *get_line_feed)(NwFontResFont *font);
  void *unk_28;
  void *unk_2C;
  void *unk_30;
  void *unk_34;
  void *unk_38;
  void *unk_3C;
  void ( *get_glyph)(NwFontResFont *font, NwFontGlyph *glyph, std::uint16_t character);
  bool ( *has_glyph)(NwFontResFont *font, std::uint16_t character);
  int ( *get_encoding)(NwFontResFont *font);
  void *unk_4C;
  void *unk_50;
  void *unk_54;
  void ( *set_linear_filter)(NwFontResFont *font, bool at_small, bool at_large);
  void *unk_5C;
  void *unk_60;
  void *unk_64;
  int ( *get_glyph_sheet_count)(NwFontResFont *font);
};
#pragma pack(pop)

#pragma pack(push, 1)

struct NwFontResFont
{
  NwFontResFontVtable *vptr;            
  void *resource;
  NwFontFinf *font_info;
  NwFontGlyphTextureDescriptor *glyph_texture_descriptors;
  unsigned int filter_flags;
  std::uint16_t cached_character;
  std::uint16_t cached_glyph;
};
#pragma pack(pop)

#pragma pack(push, 1)

struct NwFontGlyphTextureDescriptor
{
  unsigned int texture_handle;
  NwFontResFont *font;
  unsigned int physical_address_div8;
  std::uint16_t height;
  std::uint16_t width;
  std::uint8_t gpu_format;
  std::uint8_t unk_11[3];
};
#pragma pack(pop)

#pragma pack(push, 1)

struct NwFontGlyph
{
  void *sheet_data;                     
  NwFontCharWidths widths;
  std::uint8_t height;
  std::uint16_t sheet_width;
  std::uint16_t sheet_height;
  std::uint16_t cell_x;
  std::uint16_t cell_y;
  std::uint16_t sheet_format;
  std::uint8_t unk_12[2];
  NwFontGlyphTextureDescriptor *texture_descriptor;
};
#pragma pack(pop)

#pragma pack(push, 1)

struct NwFontTagProcessorVtable
{
  void *unk_0;                          
  void *unk_4;
  int ( *process)(NwFontTagProcessor *processor, std::uint16_t character, void *context);
  int ( *calc_rect)(NwFontTagProcessor *processor, float *rectangle, std::uint16_t character, void *context);
};
#pragma pack(pop)

#pragma pack(push, 1)

struct NwFontTagProcessor
{
  NwFontTagProcessorVtable *vptr;       
};
#pragma pack(pop)

#pragma pack(push, 1)

struct NwFontTextBatchGlyph
{
  float width;
  float height_delta;
  float x;
  float y;
  unsigned int top_color;
  unsigned int bottom_color;
  float u0;
  float v0;
  float u1;
  float v1;
  NwFontGlyphTextureDescriptor *texture_descriptor;
};
#pragma pack(pop)

#pragma pack(push, 1)


struct NwFontTextBatchPrefix
{
  unsigned int max_chars;               
  std::uint16_t glyph_count;
  std::uint16_t draw_glyph_limit;
  std::uint8_t built;
  std::uint8_t flags;
  std::uint8_t unk_A[2];
  std::uint8_t *glyph_bits;
  unsigned int *commands;
  unsigned int *command_buffer;
  unsigned int *aux_buffer;
  unsigned int command_words;
  unsigned int command_word_capacity;
  unsigned int first_color_command_word;
  NwFontTextBatchGlyph glyphs[];
};
#pragma pack(pop)

#pragma pack(push, 1)

struct NwLytAlphaCompare
{
  std::uint8_t compare_function;
  std::uint8_t unk_1[3];
  float reference;
};
#pragma pack(pop)

struct NwLytAnimTransform;

#pragma pack(push, 1)

struct NwLytPaneLink
{
  NwLytPaneLink *next;                  
  NwLytPaneLink *prev;
};
#pragma pack(pop)

#pragma pack(push, 1)

struct NwLytAnimationLink
{
  NwLytPaneLink link;
  NwLytAnimTransform *transform;
  std::uint16_t target_index;
  std::uint8_t disabled;
  std::uint8_t unk_F;
};
#pragma pack(pop)

#pragma pack(push, 1)

struct NwLytAnimationLinkList
{
  unsigned int count;
  NwLytPaneLink sentinel;
};
#pragma pack(pop)

#pragma pack(push, 1)

struct NwLytBlendMode
{
  std::uint8_t operation;
  std::uint8_t source_factor;
  std::uint8_t destination_factor;
  std::uint8_t logical_operation;
};
#pragma pack(pop)

struct NwLytDrawContext;

#pragma pack(push, 1)

struct NwLytDrawInfoVtable
{
  void *unk_0;                          
  void *unk_4;
};
#pragma pack(pop)

#pragma pack(push, 1)

struct NwLytTextWork
{
  std::uint8_t unk_0[316];           
  NwFontTextBatchPrefix *batch;
  std::uint8_t unk_140[36];
  float pane_mtx[3][4];
  std::uint8_t unk_194[336];
};
#pragma pack(pop)

#pragma pack(push, 1)

struct NwLytDrawInfo
{
  NwLytDrawInfoVtable *vptr;            
  float projection_mtx[4][4];
  float view_mtx[3][4];
  float location_adjust_scale[2];
  float global_alpha;
  NwLytTextWork *text_work;
  std::uint8_t unk_84[4];
  std::uint8_t flags;
  std::uint8_t unk_89[3];
};
#pragma pack(pop)

#pragma pack(push, 1)

struct NwLytDrawer
{
  std::uint8_t unk_0[4];             
};
#pragma pack(pop)

#pragma pack(push, 1)

struct NwLytMaterialVtable
{
  void *unk_0;                          
  void *unk_4;
  void *unk_8;
  void ( *unbind_animation)(NwLytMaterial *material, NwLytAnimTransform *transform);
  void *unk_10;
  void ( *animate)(NwLytMaterial *material);
  void *unk_18;
  void *unk_1C;
  void *unk_20;
  void *unk_24;
  void *unk_28;
};
#pragma pack(pop)

#pragma pack(push, 1)

struct NwLytTexMap
{
  std::uint8_t unk_0[4];
  unsigned int texture_address;
  std::uint16_t image_width;
  std::uint16_t image_height;
  std::uint16_t texture_width;
  std::uint16_t texture_height;
  unsigned int sampler_bits;
  unsigned int gpu_sampler_bits;
  unsigned int gpu_dimensions;
  unsigned int gpu_format;
};
#pragma pack(pop)

#pragma pack(push, 1)


struct NwLytMaterial
{
  NwLytMaterialVtable *vptr;            
  NwLytAnimationLinkList animation_links;
  unsigned int colors[7];
  unsigned int capacity_bits;
  unsigned int count_bits;
  NwLytTexMap *variable_data;
  std::uint8_t unk_38[21];
  std::uint8_t flags;
  std::uint8_t unk_4E[2];
};
#pragma pack(pop)

enum NwLytMaterialCountMask : std::uint32_t
{
  NwLytMaterialCountMask_TexMaps = 0x3,
  NwLytMaterialCountMask_TexSrts = 0xC,
  NwLytMaterialCountMask_TexCoordGens = 0x30,
  NwLytMaterialCountMask_TevStages = 0x1C0,
  NwLytMaterialCountMask_AlphaCompare = 0x200,
  NwLytMaterialCountMask_BlendMode = 0x400,
};
static_assert(sizeof(NwLytMaterialCountMask) == 4);

#pragma pack(push, 1)

struct NwLytPaneVtable
{
  void *unk_0;                          
  void *unk_4;
  void *unk_8;
  void *unk_C;
  void *unk_10;
  void *unk_14;
  void *unk_18;
  void *unk_1C;
  void *unk_20;
  void *unk_24;
  void *unk_28;
  void *unk_2C;
  void *unk_30;
  void ( *animate_recursive)(NwLytPane *pane, unsigned int flags);
  void ( *animate_self)(NwLytPane *pane, unsigned int flags);
  void *unk_3C;
  void *unk_40;
  void *unk_44;
  void ( *unbind_own_animations)(NwLytPane *pane, NwLytAnimTransform *transform);
  NwLytAnimationLink *( *find_animation_link)(NwLytPane *pane, NwLytAnimTransform *transform);
  void *unk_50;
  void *unk_54;
  void *unk_58;
  void ( *calculate_mtx)(NwLytPane *pane, NwLytDrawInfo *draw_info);
  void *unk_60;
  void *unk_64;
  unsigned int *( *draw_self)(NwLytPane *pane, unsigned int *cursor, NwLytDrawInfo *draw_info, NwLytDrawContext *draw_context);
  void *unk_6C;
};
#pragma pack(pop)

#pragma pack(push, 1)

struct NwLytPane
{
  NwLytPaneVtable *vptr;                
  NwLytPaneLink sibling_link;
  NwLytPane *parent;
  std::uint8_t unk_10[4];
  NwLytPaneLink children;
  NwLytAnimationLinkList animation_links;
  float translate[3];
  float rotate[3];
  float scale[2];
  float width;
  float height;
  float local_mtx[3][4];
  float global_mtx[3][4];
  std::uint8_t unk_B0[4];
  std::uint8_t alpha;
  std::uint8_t global_alpha;
  std::uint8_t base_position;
  std::uint8_t flags;
  std::uint8_t unk_B8[28];
};
#pragma pack(pop)

#pragma pack(push, 1)

struct NwLytTexCoordQuad
{
  float coordinates[4][2];              
};
#pragma pack(pop)

#pragma pack(push, 1)

struct NwLytPicture
{
  NwLytPane pane;                       
  std::uint8_t uv_calculated;
  std::uint8_t unk_D5[3];
  unsigned int uv_output_count;
  float uv_output[6][4];
  NwLytMaterial *material;
  unsigned int vertex_colors[4];
  std::uint8_t tex_coord_capacity;
  std::uint8_t tex_coord_count;
  std::uint8_t unk_152[2];
  NwLytTexCoordQuad *tex_coords;
};
#pragma pack(pop)

#pragma pack(push, 1)

struct NwLytPictureVtable
{
  NwLytPaneVtable pane;                 
  void *unk_70;
  void *unk_74;
};
#pragma pack(pop)

#pragma pack(push, 1)

struct NwLytTevStage
{
  unsigned int unk_0;
  unsigned int unk_4;
  unsigned int unk_8;
};
#pragma pack(pop)

#pragma pack(push, 1)

struct NwLytTexCoordGen
{
  std::uint8_t unk_0;
  std::uint8_t coordinate_set;
  std::uint8_t unk_2[2];
};
#pragma pack(pop)

#pragma pack(push, 1)

struct NwLytTexSrt
{
  float translate[2];
  float rotate;
  float scale[2];
};
#pragma pack(pop)

#pragma pack(push, 1)

struct NwLytTextBox
{
  NwLytPane pane;                       
  std::uint16_t *string;
  unsigned int top_color;
  unsigned int bottom_color;
  NwFontResFont *font;
  float font_width;
  float font_height;
  float line_space;
  float char_space;
  NwFontTagProcessor *tag_processor;
  std::uint16_t string_buffer_bytes;
  std::uint16_t string_length;
  std::uint8_t text_position;
  std::uint8_t text_alignment;
  std::uint8_t dirty;
  std::uint8_t unk_FF[1];
  NwLytMaterial *material;
  NwFontTextBatchPrefix *batch;
};
#pragma pack(pop)

#pragma pack(push, 1)

struct NwLytTextBoxVtable
{
  NwLytPaneVtable pane;                 
  void ( *alloc_string_buffer)(NwLytTextBox *textbox, unsigned int max_chars, unsigned int flags);
  void ( *free_string_buffer)(NwLytTextBox *textbox);
  void *unk_78;
  void *unk_7C;
};
#pragma pack(pop)

#pragma pack(push, 1)

struct SeadExpHeapBlock
{
  SeadExpHeapBlock *prev;               
  SeadExpHeapBlock *next;
  std::uint8_t unk_8[2];
  std::uint16_t alignment_offset;
  unsigned int payload_bytes;
};
#pragma pack(pop)

#pragma pack(push, 1)

struct SeadExpHeapList
{
  SeadExpHeapBlock *tail_link;          
  SeadExpHeapBlock *head_link;
  unsigned int count;
  int link_offset;
};
#pragma pack(pop)

#pragma pack(push, 1)

struct SeadExpHeapVtable
{
  void *unk_0;                          
  void *unk_4;
  void *unk_8;
  void *unk_C;
  void *unk_10;
  void *unk_14;
  void *( *try_alloc)(SeadExpHeapPrefix *heap, unsigned int bytes, int alignment);
  void ( *free)(SeadExpHeapPrefix *heap, void *pointer);
  void *unk_20;
  void *unk_24;
  void *unk_28;
  void *unk_2C;
  void *unk_30;
  void *unk_34;
  void *unk_38;
  unsigned int ( *get_free_size)(SeadExpHeapPrefix *heap);
  unsigned int ( *get_max_allocatable_size)(SeadExpHeapPrefix *heap, int alignment);
  void *unk_44;
  void *unk_48;
  void *unk_4C;
  void *unk_50;
  void *unk_54;
  void *unk_58;
  void *unk_5C;
  void *unk_60;
  void *unk_64;
  void *unk_68;
};
#pragma pack(pop)

#pragma pack(push, 1)

struct SeadExpHeapPrefix
{
  SeadExpHeapVtable *vptr;              
  std::uint8_t unk_4[116];
  SeadExpHeapList free_blocks;
  SeadExpHeapList used_blocks;
};
#pragma pack(pop)

#pragma pack(push, 1)

struct SsysMaAllocatorVtable
{
  void *unk_0;                          
  void *unk_4;
  void *( *alloc)(SsysMaAllocator *allocator, unsigned int bytes, int alignment);
  void ( *free)(SsysMaAllocator *allocator, void *pointer);
};
#pragma pack(pop)

#pragma pack(push, 1)

struct SsysMaAllocator
{
  SsysMaAllocatorVtable *vptr;          
  SeadExpHeapPrefix *heap;
};
#pragma pack(pop)

#pragma pack(push, 1)

struct SsysMaLayoutNodeVtable
{
  void *unk_0;
  void *unk_4;
  void ( *replay_recorded_list)(SsysMaLayoutNode *layout);
  void ( *calculate)(SsysMaLayoutNode *layout);
  bool ( *build)(SsysMaLayoutNode *layout, const char *name, void *resource_holder, unsigned int command_bytes);
  void ( *record_pane_tree)(SsysMaLayoutNode *layout);
  void ( *setup_projection_matrix)(SsysMaLayoutNode *layout);
};
#pragma pack(pop)

#pragma pack(push, 1)


struct SsysMaLayoutNode
{
  SsysMaLayoutNodeVtable *vptr;         
  SsysMaLayoutNode *prev;
  SsysMaLayoutNode *next;
  std::uint8_t priority;
  std::uint8_t unk_D[19];
  NwLytPane *root;
  std::uint8_t unk_24[4];
  float width;
  float height;
  NwLytDrawInfo draw_info;
  float inverse_view_mtx[3][4];
  std::uint8_t unk_EC[4];
  float view_eye[3];
  unsigned int animation_flags;
  unsigned int command_list_id;
  unsigned int record_offset;
  unsigned int record_bytes;
  unsigned int record_gx_offset;
  unsigned int record_gx_count;
  unsigned int replay_state_mask;
  unsigned int command_capacity_bytes;
  std::uint8_t replay_option;
  std::uint8_t record_dirty;
  std::uint8_t unk_11E[1];
  std::uint8_t projection_ready;
  std::uint8_t stereo_enabled;
  std::uint8_t projection_mode;
  std::uint8_t unk_122[18];
  unsigned int display;
  std::uint8_t unk_138[20];
};
#pragma pack(pop)

#pragma pack(push, 1)

struct SsysMaLayoutList
{
  std::uint8_t unk_0[4];             
  SsysMaLayoutNode *head;
  SsysMaLayoutNode *tail;
};
#pragma pack(pop)

#pragma pack(push, 1)

struct SsysMaLayoutMgr
{
  std::uint8_t unk_0[4];             
  SsysMaLayoutList top;
  SsysMaLayoutList bottom;
  NwLytTextWork text_work;
  std::uint8_t unk_300[100];
  NwLytDrawer drawer;
};
#pragma pack(pop)

using GpuCmd_GetProperty_t = void (*)(unsigned int property, unsigned int *output);
inline constexpr std::uintptr_t GpuCmd_GetProperty_addr = 0x00127EDC;

using GpuCmd_CreateLists_t = void (*)(int count, unsigned int *ids);
inline constexpr std::uintptr_t GpuCmd_CreateLists_addr = 0x00121B34;

using GpuCmd_SelectList_t = void (*)(unsigned int id);
inline constexpr std::uintptr_t GpuCmd_SelectList_addr = 0x001216BC;

using GpuCmd_AllocateListStorage_t = void (*)(unsigned int command_bytes, int gx_capacity);
inline constexpr std::uintptr_t GpuCmd_AllocateListStorage_addr = 0x00121988;

using GpuCmd_DestroyLists_t = void (*)(int count, const unsigned int *ids);
inline constexpr std::uintptr_t GpuCmd_DestroyLists_addr = 0x00127D98;

using ssys_ma_Allocator_Alloc_t = void * (*)(SsysMaAllocator *allocator, unsigned int bytes, int alignment);
inline constexpr std::uintptr_t ssys_ma_Allocator_Alloc_addr = 0x00569B54;

using ssys_ma_Allocator_Free_t = void (*)(SsysMaAllocator *allocator, void *pointer);
inline constexpr std::uintptr_t ssys_ma_Allocator_Free_addr = 0x00569B44;

using ssys_ma_lyt_Layout_RecordPaneTree_t = void (*)(SsysMaLayoutNode *layout);
inline constexpr std::uintptr_t ssys_ma_lyt_Layout_RecordPaneTree_addr = 0x005686D4;

using ssys_ma_lyt_Layout_ReplayRecordedList_t = void (*)(SsysMaLayoutNode *layout);
inline constexpr std::uintptr_t ssys_ma_lyt_Layout_ReplayRecordedList_addr = 0x005682DC;

using ssys_ma_lyt_Layout_SetupProjectionMatrix_t = void (*)(SsysMaLayoutNode *layout);
inline constexpr std::uintptr_t ssys_ma_lyt_Layout_SetupProjectionMatrix_addr = 0x00567BC0;

using ssys_ma_lyt_LayoutMgr_AddLayout_t = void (*)(SsysMaLayoutMgr *manager, SsysMaLayoutNode *layout, unsigned int screen);
inline constexpr std::uintptr_t ssys_ma_lyt_LayoutMgr_AddLayout_addr = 0x0056928C;

using nwfont_TextBatch_Init_t = NwFontTextBatchPrefix * (*)(NwFontTextBatchPrefix *batch, unsigned int max_chars);
inline constexpr std::uintptr_t nwfont_TextBatch_Init_addr = 0x004D76BC;

using nwlyt_TextBox_CalcDrawBufferBytes_t = unsigned int (*)(unsigned int max_chars);
inline constexpr std::uintptr_t nwlyt_TextBox_CalcDrawBufferBytes_addr = 0x004D5480;

using nwlyt_Picture_DrawToCommandList_t = unsigned int * (*)(NwLytPicture *picture, unsigned int *cursor, NwLytDrawInfo *draw_info, NwLytDrawContext *draw_context);
inline constexpr std::uintptr_t nwlyt_Picture_DrawToCommandList_addr = 0x0073CB58;

// プロトタイプ未設定
inline constexpr std::uintptr_t DrawCtx_FlushBatch_addr = 0x004B8360;

using g_LayoutMgr_t = SsysMaLayoutMgr *;
inline constexpr std::uintptr_t g_LayoutMgr_addr = 0x0096FC38;

using g_LayoutAllocator_t = SsysMaAllocator *;
inline constexpr std::uintptr_t g_LayoutAllocator_addr = 0x0096F150;

using vtbl_ssys_ma_lyt_Layout_t = SsysMaLayoutNodeVtable;
inline constexpr std::uintptr_t vtbl_ssys_ma_lyt_Layout_addr = 0x00900608;

using vtbl_nw_lyt_Pane_t = NwLytPaneVtable;
inline constexpr std::uintptr_t vtbl_nw_lyt_Pane_addr = 0x008FBE7C;

using vtbl_nw_lyt_Picture_t = NwLytPictureVtable;
inline constexpr std::uintptr_t vtbl_nw_lyt_Picture_addr = 0x008FBFFC;

using vtbl_nw_lyt_TextBox_t = NwLytTextBoxVtable;
inline constexpr std::uintptr_t vtbl_nw_lyt_TextBox_addr = 0x008FC07C;

using vtbl_nw_lyt_Material_t = NwLytMaterialVtable;
inline constexpr std::uintptr_t vtbl_nw_lyt_Material_addr = 0x008FC18C;

using vtbl_nw_lyt_DrawInfo_t = NwLytDrawInfoVtable;
inline constexpr std::uintptr_t vtbl_nw_lyt_DrawInfo_addr = 0x008FC17C;

using vtbl_nw_font_ResFont_t = NwFontResFontVtable;
inline constexpr std::uintptr_t vtbl_nw_font_ResFont_addr = 0x008FCAAC;

using vtbl_nw_font_TagProcessorBase_wchar_t_t = NwFontTagProcessorVtable;
inline constexpr std::uintptr_t vtbl_nw_font_TagProcessorBase_wchar_t_addr = 0x008FCA24;

using g_DefaultWideTagProcessor_t = NwFontTagProcessor;
inline constexpr std::uintptr_t g_DefaultWideTagProcessor_addr = 0x0097C0B4;

using vtbl_sead_ExpHeap_t = SeadExpHeapVtable;
inline constexpr std::uintptr_t vtbl_sead_ExpHeap_addr = 0x008FFEF8;

inline constexpr std::uintptr_t FrameDispatch_addr = 0x001B721C;
inline constexpr std::uint32_t FrameDispatch_len = 16;
inline constexpr std::uint32_t FrameDispatch_crc32 = 0x92DE8A91;

inline constexpr std::uintptr_t LayoutTopEntry_addr = 0x00566DF8;
inline constexpr std::uint32_t LayoutTopEntry_len = 16;
inline constexpr std::uint32_t LayoutTopEntry_crc32 = 0xC0033266;

inline constexpr std::uintptr_t LayoutBottomEntry_addr = 0x00568F98;
inline constexpr std::uint32_t LayoutBottomEntry_len = 16;
inline constexpr std::uint32_t LayoutBottomEntry_crc32 = 0x38F20DB7;

inline constexpr std::uintptr_t GpuPropertyEntry_addr = 0x00127EDC;
inline constexpr std::uint32_t GpuPropertyEntry_len = 16;
inline constexpr std::uint32_t GpuPropertyEntry_crc32 = 0x909B8572;

inline constexpr std::uintptr_t GpuCreateEntry_addr = 0x00121B34;
inline constexpr std::uint32_t GpuCreateEntry_len = 16;
inline constexpr std::uint32_t GpuCreateEntry_crc32 = 0x1138EC2A;

inline constexpr std::uintptr_t GpuSelectEntry_addr = 0x001216BC;
inline constexpr std::uint32_t GpuSelectEntry_len = 16;
inline constexpr std::uint32_t GpuSelectEntry_crc32 = 0x3F4C5335;

inline constexpr std::uintptr_t GpuStorageEntry_addr = 0x00121988;
inline constexpr std::uint32_t GpuStorageEntry_len = 16;
inline constexpr std::uint32_t GpuStorageEntry_crc32 = 0xA859D78C;

inline constexpr std::uintptr_t GpuDestroyEntry_addr = 0x00127D98;
inline constexpr std::uint32_t GpuDestroyEntry_len = 16;
inline constexpr std::uint32_t GpuDestroyEntry_crc32 = 0x92C31303;

inline constexpr std::uintptr_t LayoutAllocateEntry_addr = 0x00569B54;
inline constexpr std::uint32_t LayoutAllocateEntry_len = 16;
inline constexpr std::uint32_t LayoutAllocateEntry_crc32 = 0xB9B101E8;

inline constexpr std::uintptr_t LayoutFreeEntry_addr = 0x00569B44;
inline constexpr std::uint32_t LayoutFreeEntry_len = 16;
inline constexpr std::uint32_t LayoutFreeEntry_crc32 = 0x8CC1F8F9;

inline constexpr std::uintptr_t LayoutRecordEntry_addr = 0x005686D4;
inline constexpr std::uint32_t LayoutRecordEntry_len = 16;
inline constexpr std::uint32_t LayoutRecordEntry_crc32 = 0x991F6C1C;

inline constexpr std::uintptr_t LayoutReplayEntry_addr = 0x005682DC;
inline constexpr std::uint32_t LayoutReplayEntry_len = 16;
inline constexpr std::uint32_t LayoutReplayEntry_crc32 = 0xC6C2DDE5;

inline constexpr std::uintptr_t LayoutAddEntry_addr = 0x0056928C;
inline constexpr std::uint32_t LayoutAddEntry_len = 16;
inline constexpr std::uint32_t LayoutAddEntry_crc32 = 0xD672CD75;

inline constexpr std::uintptr_t LayoutProjectionEntry_addr = 0x00567BC0;
inline constexpr std::uint32_t LayoutProjectionEntry_len = 16;
inline constexpr std::uint32_t LayoutProjectionEntry_crc32 = 0x16D02DC7;

inline constexpr std::uintptr_t TextBatchInitEntry_addr = 0x004D76BC;
inline constexpr std::uint32_t TextBatchInitEntry_len = 16;
inline constexpr std::uint32_t TextBatchInitEntry_crc32 = 0x5E9A8CD3;

inline constexpr std::uintptr_t TextBufferBytesEntry_addr = 0x004D5480;
inline constexpr std::uint32_t TextBufferBytesEntry_len = 16;
inline constexpr std::uint32_t TextBufferBytesEntry_crc32 = 0xEB17487B;

inline constexpr std::uintptr_t PictureDrawEntry_addr = 0x0073CB58;
inline constexpr std::uint32_t PictureDrawEntry_len = 32;
inline constexpr std::uint32_t PictureDrawEntry_crc32 = 0x3C0D5FFA;

inline constexpr std::uintptr_t FlushBatchEntry_addr = 0x004B8360;
inline constexpr std::uint32_t FlushBatchEntry_len = 32;
inline constexpr std::uint32_t FlushBatchEntry_crc32 = 0xF0F18EEB;

} // namespace gohan::game::generated
