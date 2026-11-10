// 生成物。編集しない（export_game_api.py が作り直す）。
// build: acnl_jpn_v15
// code SHA256: c635af8f08937b66f123b91b02391a68c034393320cdb94d603b9291383faabb
#pragma once
#include <cstdint>

namespace gohan::game::generated {

struct AcnlEncryptedValueF010;
struct ItemID;
struct AcnlPlayerSave;
struct AcnlGardenSavePrefix;
struct AcnlPadReaderPrefix;
struct AcnlPadState;
struct AcnlPlayerInventoryFinanceF010;
struct AcnlUiPoint2;
struct AcnlUiPoint2I;
struct SeadControllerBasePrefix;
struct DropData;
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
struct SeadOffsetList;
struct SeadControllerPrefix;
struct SeadControllerWrapperPrefix;
struct SeadCtrControllerVtable;
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

#pragma pack(push, 2)

struct AcnlEncryptedValueF010
{
  unsigned int ciphertext;              
  std::uint16_t salt;                
  std::uint8_t rotation;             
  std::uint8_t checksum;             
};
#pragma pack(pop)

#pragma pack(push, 2)
struct ItemID
{
  std::uint16_t id;
  std::uint16_t flags;
};
#pragma pack(pop)

#pragma pack(push, 4)

struct AcnlPlayerSave
{
  std::uint8_t unk_0[27532];
  AcnlEncryptedValueF010 bank;          
  std::uint8_t unk_6B94[8];
  AcnlEncryptedValueF010 medals;        
  std::uint8_t unk_6BA4[868];
  AcnlEncryptedValueF010 wallet;        
  std::uint8_t unk_6F10[7692];
  AcnlEncryptedValueF010 meow_coupons;  
  std::uint8_t unk_8D24[5980];
};
#pragma pack(pop)

#pragma pack(push, 4)

struct AcnlGardenSavePrefix
{
  std::uint8_t unk_0[160];
  AcnlPlayerSave players[4];            
  std::uint8_t unk_292A0[269120];
  AcnlEncryptedValueF010 turnip_prices[12]; 
};
#pragma pack(pop)

#pragma pack(push, 2)

struct AcnlPadReaderPrefix
{
  void *handle;
  unsigned int unk_4;
  unsigned int previous_held;
  std::uint8_t unk_C[36];
  bool resync_previous_held_pending;    
};
#pragma pack(pop)

#pragma pack(push, 2)

struct AcnlPadState
{
  unsigned int held;
  unsigned int pressed;
  unsigned int released;
  std::int16_t circle_x;
  std::int16_t circle_y;
};
#pragma pack(pop)

#pragma pack(push, 2)
struct AcnlPlayerInventoryFinanceF010
{
  std::uint8_t unknown0[27532];
  AcnlEncryptedValueF010 bank;
  AcnlEncryptedValueF010 loan;
  AcnlEncryptedValueF010 medals;
  std::uint8_t unknown_after_medals[44];
  ItemID pocket[16];
  std::uint8_t pocket_flags[16];
  std::uint8_t unknown_after_pocket[744];
  AcnlEncryptedValueF010 wallet;
  std::uint8_t unknown_after_wallet[9184];
  ItemID storage[180];
};
#pragma pack(pop)

#pragma pack(push, 2)

struct AcnlUiPoint2
{
  float x;
  float y;
};
#pragma pack(pop)

#pragma pack(push, 2)

struct AcnlUiPoint2I
{
  int x;
  int y;
};
#pragma pack(pop)

#pragma pack(push, 2)

struct SeadControllerBasePrefix
{
  void *vtable;
  unsigned int pressed;
  unsigned int released;
  unsigned int repeat;
  unsigned int pointer_flags;           
  AcnlUiPoint2I pixel_int;
  AcnlUiPoint2 pointer_min;
  AcnlUiPoint2 pointer_max;
  unsigned int held_frames[32];
  std::uint8_t repeat_delay[32];
  std::uint8_t repeat_interval[32];
  float axis0_press_threshold;
  float axis1_press_threshold;
  float axis0_release_threshold;
  float axis1_release_threshold;
  int button_count;
  int axis0_bit;
  int axis1_bit;
  int pointer_bit;
  unsigned int idle_frames;
  unsigned int held;
  AcnlUiPoint2 pixel_float;
  AcnlUiPoint2 axis0;
  AcnlUiPoint2 axis1;
  float unk_12C[2];
};
#pragma pack(pop)

enum AcnlPointerFlags : std::uint32_t
{
  AcnlPointerFlags_Valid = 0x1,
  AcnlPointerFlags_Pressed = 0x2,
  AcnlPointerFlags_Released = 0x4,
  AcnlPointerFlags_ClearPosition = 0x8,
};
static_assert(sizeof(AcnlPointerFlags) == 4);

typedef std::uint32_t u32;

typedef std::uint8_t u8;

#pragma pack(push, 2)
struct DropData
{
  struct ItemID ItemToReplace;
  struct ItemID ItemToPlace;
  struct ItemID ItemToShow;
  std::uint8_t wX;
  std::uint8_t wY;
  std::uint8_t DropType;
  std::uint8_t State;
  std::uint8_t Slot;
  std::uint8_t Flags;
};
#pragma pack(pop)

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

#pragma pack(push, 2)

struct SeadOffsetList
{
  void *prev;
  void *next;
  unsigned int count;
  unsigned int offset;
};
#pragma pack(pop)

#pragma pack(push, 2)

struct SeadControllerPrefix
{
  SeadControllerBasePrefix base;
  std::uint8_t unk_134;
  std::uint8_t unk_135[3];
  void *manager;
  SeadOffsetList addons;
  SeadOffsetList wrappers;
};
#pragma pack(pop)

#pragma pack(push, 2)

struct SeadControllerWrapperPrefix
{
  SeadControllerBasePrefix base;
  std::uint8_t unk_134[16];
  SeadControllerPrefix *source;
  std::uint8_t enabled;
  std::uint8_t unk_149[43];
  std::uint8_t source_bit_index[32];
};
#pragma pack(pop)

struct SeadCtrControllerVtable
{
  void *unk_0;                          
  void *unk_4;
  void *unk_8;
  void *unk_C;
  int ( *calc)(SeadControllerPrefix *controller);
  void *unk_14;
  void *( *update_from_device)(SeadControllerPrefix *controller);
  void *unk_1C;
  void *unk_20;
};

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

// プロトタイプ未設定
inline constexpr std::uintptr_t vc_LOADICON_addr = 0x0026DB1C;

// プロトタイプ未設定
inline constexpr std::uintptr_t Item_IsEmpty_addr = 0x002FCB24;

// プロトタイプ未設定
inline constexpr std::uintptr_t Inventory_FindEmptySlot_addr = 0x00723604;

using Inventory_GetSlot_t = struct ItemID * (*)(void *inventory, unsigned int slot, std::uint8_t *out_flag, unsigned int fallback);
inline constexpr std::uintptr_t Inventory_GetSlot_addr = 0x00723878;

// プロトタイプ未設定
inline constexpr std::uintptr_t ItemName_ResolvePhrase_addr = 0x0056D274;

// プロトタイプ未設定
inline constexpr std::uintptr_t script_WordRes_Ctor_addr = 0x005FC344;

// プロトタイプ未設定
inline constexpr std::uintptr_t vc_A_GETONLINEPLAYERINDEX_addr = 0x00305F6C;

// プロトタイプ未設定
inline constexpr std::uintptr_t vc_PLAYERINSTANCE_addr = 0x005C27D8;

// プロトタイプ未設定
inline constexpr std::uintptr_t vc_WORLDCOORDS_addr = 0x005BFCE4;

// プロトタイプ未設定
inline constexpr std::uintptr_t Actor_SetState_addr = 0x0064C688;

using Player_CopyStatePacketForEdit_t = std::uint8_t * (*)(void *actor);
inline constexpr std::uintptr_t Player_CopyStatePacketForEdit_addr = 0x006561F0;

using StatePacket_PackXZ_t = void (*)(std::int16_t *packed_xz, const float *xyz);
inline constexpr std::uintptr_t StatePacket_PackXZ_addr = 0x005D3504;

// プロトタイプ未設定
inline constexpr std::uintptr_t PlayerState_FaintEnter_addr = 0x00676284;

// プロトタイプ未設定
inline constexpr std::uintptr_t Field_SetItemAtWorldXY_addr = 0x002FC950;

// プロトタイプ未設定
inline constexpr std::uintptr_t Field_GetItemAtXY_addr = 0x002FEE38;

// プロトタイプ未設定
inline constexpr std::uintptr_t Field_GrowUpWiltBushSapling_addr = 0x003159A5;

// プロトタイプ未設定
inline constexpr std::uintptr_t Field_GrowUpWiltSapling_addr = 0x00315A11;

// プロトタイプ未設定
inline constexpr std::uintptr_t Field_GrowUpWiltSaplingToItem3_addr = 0x00316345;

// プロトタイプ未設定
inline constexpr std::uintptr_t Field_GrowUpWiltSaplingToItem4_addr = 0x003163C5;

// プロトタイプ未設定
inline constexpr std::uintptr_t Save_GetCurrentPlayer_addr = 0x002FB900;

// プロトタイプ未設定
inline constexpr std::uintptr_t vc_D_GARDEN_addr = 0x002FB354;

using PlayerSave_GetSlotIndex_t = int (*)(const void *player_array, const void *player_save);
inline constexpr std::uintptr_t PlayerSave_GetSlotIndex_addr = 0x00309F18;

// プロトタイプ未設定
inline constexpr std::uintptr_t vc_ROOMDAT_addr = 0x00308110;

using Villager_RunDailyUpdateOnce_t = void (*)();
inline constexpr std::uintptr_t Villager_RunDailyUpdateOnce_addr = 0x0030C994;

using Room_UseExit_t = int (*)(void *room_mgr, unsigned int exit_index, int transition_arg0, int transition_arg1, int transition_arg2, int transition_arg3);
inline constexpr std::uintptr_t Room_UseExit_addr = 0x005B464C;

// プロトタイプ未設定
inline constexpr std::uintptr_t Effect_DigDirt_addr = 0x00762274;

// プロトタイプ未設定
inline constexpr std::uintptr_t Effect_ScoopScrape_addr = 0x0066F584;

using FieldObj_FellObject_t = int (*)(void *a1, void *ItemToPlace, int X, int Y, int a5, int kind, int fx);
inline constexpr std::uintptr_t FieldObj_FellObject_addr = 0x0059A0B4;

// プロトタイプ未設定
inline constexpr std::uintptr_t FieldObj_SpawnActor_addr = 0x00599308;

// プロトタイプ未設定
inline constexpr std::uintptr_t Field_MarkDirty_addr = 0x0059DA7C;

// プロトタイプ未設定
inline constexpr std::uintptr_t Field_SetItemAt_addr = 0x00168E08;

// プロトタイプ未設定
inline constexpr std::uintptr_t Field_XYToWorld_addr = 0x006A68E4;

// プロトタイプ未設定
inline constexpr std::uintptr_t Item_InitWithId_addr = 0x002FCBB4;

// プロトタイプ未設定
inline constexpr std::uintptr_t Player_BeginFieldObjAction_addr = 0x00595A8C;

using Save_DecryptValue_t = unsigned int (*)(const AcnlEncryptedValueF010 *value);
inline constexpr std::uintptr_t Save_DecryptValue_addr = 0x00303530;

using Save_EncryptValue_t = unsigned int (*)(AcnlEncryptedValueF010 *value, unsigned int plaintext);
inline constexpr std::uintptr_t Save_EncryptValue_addr = 0x00303404;

using Save_GetPlayerByOnlineSlot_t = AcnlPlayerSave * (*)(unsigned int online_slot);
inline constexpr std::uintptr_t Save_GetPlayerByOnlineSlot_addr = 0x002FEB60;

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

using g_ActiveBsMenuItem_t = void *;
inline constexpr std::uintptr_t g_ActiveBsMenuItem_addr = 0x009499EC;

using vc_MAPBOOL_t = std::uint8_t;
inline constexpr std::uintptr_t vc_MAPBOOL_addr = 0x00949C30;

using g_NetGameMgrPtr_t = void *;
inline constexpr std::uintptr_t g_NetGameMgrPtr_addr = 0x0094D644;

using g_CurrentRoomId_t = std::uint8_t;
inline constexpr std::uintptr_t g_CurrentRoomId_addr = 0x0095133A;

using g_VillagerDailyUpdateGuard_t = std::uint8_t;
inline constexpr std::uintptr_t g_VillagerDailyUpdateGuard_addr = 0x00956231;

using g_TextPagePadding_t = u8[4072];
inline constexpr std::uintptr_t g_TextPagePadding_addr = 0x00838018;

using g_TreeHitCounts_t = u32[320];
inline constexpr std::uintptr_t g_TreeHitCounts_addr = 0x009AE504;

using g_DropDataTable_t = DropData[4];
inline constexpr std::uintptr_t g_DropDataTable_addr = 0x009AE15C;

using g_GardenSaveBasePtr_t = AcnlGardenSavePrefix *;
inline constexpr std::uintptr_t g_GardenSaveBasePtr_addr = 0x00955F8C;

using g_LayoutMgr_t = SsysMaLayoutMgr *;
inline constexpr std::uintptr_t g_LayoutMgr_addr = 0x0096FC38;

using g_LayoutAllocator_t = SsysMaAllocator *;
inline constexpr std::uintptr_t g_LayoutAllocator_addr = 0x0096F150;

using vtbl_sead_CtrController_t = SeadCtrControllerVtable;
inline constexpr std::uintptr_t vtbl_sead_CtrController_addr = 0x008FE6F8;

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

inline constexpr std::uintptr_t BsMenuItem_vtable = 0x008E5220;

inline constexpr std::uintptr_t script_WordRes_vtable = 0x00904954;

inline constexpr std::uintptr_t FrameDispatch_addr = 0x001B721C;
inline constexpr std::uint32_t FrameDispatch_len = 16;
inline constexpr std::uint32_t FrameDispatch_crc32 = 0x92DE8A91;

inline constexpr std::uintptr_t InputPadRead_addr = 0x003534C8;
inline constexpr std::uint32_t InputPadRead_len = 16;
inline constexpr std::uint32_t InputPadRead_crc32 = 0xF58178F8;

inline constexpr std::uintptr_t InputControllerCalc_addr = 0x0053CDE8;
inline constexpr std::uint32_t InputControllerCalc_len = 16;
inline constexpr std::uint32_t InputControllerCalc_crc32 = 0xB77ADF3C;

inline constexpr std::uintptr_t KeepSetItem_addr = 0x002FC950;
inline constexpr std::uint32_t KeepSetItem_len = 16;
inline constexpr std::uint32_t KeepSetItem_crc32 = 0x9BA5F1CD;

inline constexpr std::uintptr_t ItemNameResolve_addr = 0x0056D274;
inline constexpr std::uint32_t ItemNameResolve_len = 16;
inline constexpr std::uint32_t ItemNameResolve_crc32 = 0x4E813A66;

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

inline constexpr std::uintptr_t PatchWall0_addr = 0x0064F03C;
inline constexpr std::uint32_t PatchWall0_len = 4;
inline constexpr std::uint32_t PatchWall0_crc32 = 0x5A0DDF02;

inline constexpr std::uintptr_t PatchWall1_addr = 0x0064F19C;
inline constexpr std::uint32_t PatchWall1_len = 4;
inline constexpr std::uint32_t PatchWall1_crc32 = 0x867FCA02;

inline constexpr std::uintptr_t PatchWall2_addr = 0x0064F1B4;
inline constexpr std::uint32_t PatchWall2_len = 4;
inline constexpr std::uint32_t PatchWall2_crc32 = 0x3050A1F9;

inline constexpr std::uintptr_t PatchFlower0_addr = 0x00596890;
inline constexpr std::uint32_t PatchFlower0_len = 4;
inline constexpr std::uint32_t PatchFlower0_crc32 = 0xD5F6623D;

inline constexpr std::uintptr_t PatchTrap0_addr = 0x00659160;
inline constexpr std::uint32_t PatchTrap0_len = 4;
inline constexpr std::uint32_t PatchTrap0_crc32 = 0x035DE6AE;

inline constexpr std::uintptr_t PatchTrap1_addr = 0x006774DC;
inline constexpr std::uint32_t PatchTrap1_len = 4;
inline constexpr std::uint32_t PatchTrap1_crc32 = 0x8E7E5185;

inline constexpr std::uintptr_t PatchBedHead0_addr = 0x0020C6D8;
inline constexpr std::uint32_t PatchBedHead0_len = 4;
inline constexpr std::uint32_t PatchBedHead0_crc32 = 0xCDF6EF86;

inline constexpr std::uintptr_t PatchTrample0_addr = 0x00596920;
inline constexpr std::uint32_t PatchTrample0_len = 4;
inline constexpr std::uint32_t PatchTrample0_crc32 = 0x0E1E7958;

inline constexpr std::uintptr_t PatchTrample1_addr = 0x00596870;
inline constexpr std::uint32_t PatchTrample1_len = 4;
inline constexpr std::uint32_t PatchTrample1_crc32 = 0x2FF7667C;

inline constexpr std::uintptr_t PatchTrample2_addr = 0x0059689C;
inline constexpr std::uint32_t PatchTrample2_len = 4;
inline constexpr std::uint32_t PatchTrample2_crc32 = 0x8D84E93E;

inline constexpr std::uintptr_t PatchTrample3_addr = 0x005968D0;
inline constexpr std::uint32_t PatchTrample3_len = 4;
inline constexpr std::uint32_t PatchTrample3_crc32 = 0x649A4103;

inline constexpr std::uintptr_t PatchTrample4_addr = 0x005968D8;
inline constexpr std::uint32_t PatchTrample4_len = 4;
inline constexpr std::uint32_t PatchTrample4_crc32 = 0x66DB617F;

inline constexpr std::uintptr_t PatchTrample5_addr = 0x005968E4;
inline constexpr std::uint32_t PatchTrample5_len = 4;
inline constexpr std::uint32_t PatchTrample5_crc32 = 0x4CB28129;

inline constexpr std::uintptr_t PatchDig0_addr = 0x00595B00;
inline constexpr std::uint32_t PatchDig0_len = 4;
inline constexpr std::uint32_t PatchDig0_crc32 = 0xA2D4D157;

inline constexpr std::uintptr_t PatchDig1_addr = 0x00598294;
inline constexpr std::uint32_t PatchDig1_len = 4;
inline constexpr std::uint32_t PatchDig1_crc32 = 0x4B0F2794;

inline constexpr std::uintptr_t PatchDig2_addr = 0x00598478;
inline constexpr std::uint32_t PatchDig2_len = 4;
inline constexpr std::uint32_t PatchDig2_crc32 = 0xF08CA6B4;

inline constexpr std::uintptr_t PatchDig3_addr = 0x005984EC;
inline constexpr std::uint32_t PatchDig3_len = 4;
inline constexpr std::uint32_t PatchDig3_crc32 = 0xEC802E17;

inline constexpr std::uintptr_t PatchDig4_addr = 0x00598550;
inline constexpr std::uint32_t PatchDig4_len = 4;
inline constexpr std::uint32_t PatchDig4_crc32 = 0x44C31810;

inline constexpr std::uintptr_t PatchDig5_addr = 0x00662E88;
inline constexpr std::uint32_t PatchDig5_len = 4;
inline constexpr std::uint32_t PatchDig5_crc32 = 0x0B5E5397;

inline constexpr std::uintptr_t PatchDig6_addr = 0x00663294;
inline constexpr std::uint32_t PatchDig6_len = 4;
inline constexpr std::uint32_t PatchDig6_crc32 = 0xA417B1D3;

inline constexpr std::uintptr_t PatchDig7_addr = 0x00680F10;
inline constexpr std::uint32_t PatchDig7_len = 4;
inline constexpr std::uint32_t PatchDig7_crc32 = 0xAE2CD666;

inline constexpr std::uintptr_t PatchDig8_addr = 0x005982C4;
inline constexpr std::uint32_t PatchDig8_len = 4;
inline constexpr std::uint32_t PatchDig8_crc32 = 0x0C5F5924;

inline constexpr std::uintptr_t PatchLookUp0_addr = 0x0064C594;
inline constexpr std::uint32_t PatchLookUp0_len = 4;
inline constexpr std::uint32_t PatchLookUp0_crc32 = 0x3823E6F4;

inline constexpr std::uintptr_t PatchShop0_addr = 0x003093BC;
inline constexpr std::uint32_t PatchShop0_len = 4;
inline constexpr std::uint32_t PatchShop0_crc32 = 0xB640D037;

inline constexpr std::uintptr_t PatchShop1_addr = 0x007102C8;
inline constexpr std::uint32_t PatchShop1_len = 4;
inline constexpr std::uint32_t PatchShop1_crc32 = 0xB640D037;

inline constexpr std::uintptr_t PatchShop2_addr = 0x00710380;
inline constexpr std::uint32_t PatchShop2_len = 4;
inline constexpr std::uint32_t PatchShop2_crc32 = 0xB640D037;

inline constexpr std::uintptr_t PatchShop3_addr = 0x00712664;
inline constexpr std::uint32_t PatchShop3_len = 4;
inline constexpr std::uint32_t PatchShop3_crc32 = 0xB640D037;

inline constexpr std::uintptr_t PatchShop4_addr = 0x0071BBE0;
inline constexpr std::uint32_t PatchShop4_len = 4;
inline constexpr std::uint32_t PatchShop4_crc32 = 0xB640D037;

inline constexpr std::uintptr_t PatchShop5_addr = 0x00710000;
inline constexpr std::uint32_t PatchShop5_len = 4;
inline constexpr std::uint32_t PatchShop5_crc32 = 0xB640D037;

inline constexpr std::uintptr_t PatchShop6_addr = 0x0071DE08;
inline constexpr std::uint32_t PatchShop6_len = 4;
inline constexpr std::uint32_t PatchShop6_crc32 = 0xB640D037;

inline constexpr std::uintptr_t PatchShop7_addr = 0x0071684C;
inline constexpr std::uint32_t PatchShop7_len = 4;
inline constexpr std::uint32_t PatchShop7_crc32 = 0xB640D037;

inline constexpr std::uintptr_t PatchShop8_addr = 0x00716BF8;
inline constexpr std::uint32_t PatchShop8_len = 4;
inline constexpr std::uint32_t PatchShop8_crc32 = 0xB640D037;

inline constexpr std::uintptr_t PatchText0_addr = 0x005F8278;
inline constexpr std::uint32_t PatchText0_len = 4;
inline constexpr std::uint32_t PatchText0_crc32 = 0x0D80C792;

inline constexpr std::uintptr_t PatchFps0_addr = 0x0054C6E8;
inline constexpr std::uint32_t PatchFps0_len = 4;
inline constexpr std::uint32_t PatchFps0_crc32 = 0xA9629FF0;

inline constexpr std::uintptr_t PatchTree0_addr = 0x0059BBB0;
inline constexpr std::uint32_t PatchTree0_len = 4;
inline constexpr std::uint32_t PatchTree0_crc32 = 0x97E5B936;

inline constexpr std::uintptr_t PatchDig30_addr = 0x005A09EC;
inline constexpr std::uint32_t PatchDig30_len = 4;
inline constexpr std::uint32_t PatchDig30_crc32 = 0x8314B55F;

inline constexpr std::uintptr_t PatchDig31_addr = 0x0065BD58;
inline constexpr std::uint32_t PatchDig31_len = 4;
inline constexpr std::uint32_t PatchDig31_crc32 = 0x11900BAC;

inline constexpr std::uintptr_t PatchDig32_addr = 0x0065BDEC;
inline constexpr std::uint32_t PatchDig32_len = 4;
inline constexpr std::uint32_t PatchDig32_crc32 = 0x37C32C10;

inline constexpr std::uintptr_t PatchDig33_addr = 0x0065BD2C;
inline constexpr std::uint32_t PatchDig33_len = 4;
inline constexpr std::uint32_t PatchDig33_crc32 = 0xAA0AB6B5;

inline constexpr std::uintptr_t WeatherInstruction_addr = 0x0062E728;
inline constexpr std::uint32_t WeatherInstruction_len = 4;
inline constexpr std::uint32_t WeatherInstruction_crc32 = 0xD72C264C;

inline constexpr std::uintptr_t SaveDecryptEntry_addr = 0x00303530;
inline constexpr std::uint32_t SaveDecryptEntry_len = 4;
inline constexpr std::uint32_t SaveDecryptEntry_crc32 = 0xF83B0570;

inline constexpr std::uintptr_t SaveEncryptEntry_addr = 0x00303404;
inline constexpr std::uint32_t SaveEncryptEntry_len = 4;
inline constexpr std::uint32_t SaveEncryptEntry_crc32 = 0xC31DF273;

inline constexpr std::uintptr_t SavePlayerEntry_addr = 0x002FEB60;
inline constexpr std::uint32_t SavePlayerEntry_len = 4;
inline constexpr std::uint32_t SavePlayerEntry_crc32 = 0xF83B0570;

inline constexpr std::uintptr_t LocalIndexEntry_addr = 0x00305F6C;
inline constexpr std::uint32_t LocalIndexEntry_len = 4;
inline constexpr std::uint32_t LocalIndexEntry_crc32 = 0x889FC869;

inline constexpr std::uintptr_t LocalActorEntry_addr = 0x005C27D8;
inline constexpr std::uint32_t LocalActorEntry_len = 4;
inline constexpr std::uint32_t LocalActorEntry_crc32 = 0x2D9EA6DD;

inline constexpr std::uintptr_t WorldCoordsEntry_addr = 0x005BFCE4;
inline constexpr std::uint32_t WorldCoordsEntry_len = 4;
inline constexpr std::uint32_t WorldCoordsEntry_crc32 = 0xC31DF273;

inline constexpr std::uintptr_t CopyStateEntry_addr = 0x006561F0;
inline constexpr std::uint32_t CopyStateEntry_len = 4;
inline constexpr std::uint32_t CopyStateEntry_crc32 = 0xC31DF273;

inline constexpr std::uintptr_t PackXZEntry_addr = 0x005D3504;
inline constexpr std::uint32_t PackXZEntry_len = 4;
inline constexpr std::uint32_t PackXZEntry_crc32 = 0x9A327F54;

inline constexpr std::uintptr_t SetStateEntry_addr = 0x0064C688;
inline constexpr std::uint32_t SetStateEntry_len = 4;
inline constexpr std::uint32_t SetStateEntry_crc32 = 0x393EA005;

inline constexpr std::uintptr_t FaintEntry_addr = 0x00676284;
inline constexpr std::uint32_t FaintEntry_len = 4;
inline constexpr std::uint32_t FaintEntry_crc32 = 0x2F862E7F;

inline constexpr std::uintptr_t GetTileEntry_addr = 0x002FEE38;
inline constexpr std::uint32_t GetTileEntry_len = 4;
inline constexpr std::uint32_t GetTileEntry_crc32 = 0x25180375;

inline constexpr std::uintptr_t KeepBushCaller_addr = 0x003159F2;
inline constexpr std::uint32_t KeepBushCaller_len = 4;
inline constexpr std::uint32_t KeepBushCaller_crc32 = 0x62EACA23;

inline constexpr std::uintptr_t KeepSaplingCaller0_addr = 0x00315A7A;
inline constexpr std::uint32_t KeepSaplingCaller0_len = 4;
inline constexpr std::uint32_t KeepSaplingCaller0_crc32 = 0xCD7CD869;

inline constexpr std::uintptr_t KeepSaplingCaller1_addr = 0x00315B08;
inline constexpr std::uint32_t KeepSaplingCaller1_len = 4;
inline constexpr std::uint32_t KeepSaplingCaller1_crc32 = 0xF5DC1D64;

inline constexpr std::uintptr_t KeepItem3Caller_addr = 0x00316392;
inline constexpr std::uint32_t KeepItem3Caller_len = 4;
inline constexpr std::uint32_t KeepItem3Caller_crc32 = 0x3DBF475A;

inline constexpr std::uintptr_t KeepItem4Caller_addr = 0x00316412;
inline constexpr std::uint32_t KeepItem4Caller_len = 4;
inline constexpr std::uint32_t KeepItem4Caller_crc32 = 0xCDC6085F;

inline constexpr std::uintptr_t KeepFootprint_addr = 0x0052734C;
inline constexpr std::uint32_t KeepFootprint_len = 4;
inline constexpr std::uint32_t KeepFootprint_crc32 = 0xE4FA69DE;

inline constexpr std::uintptr_t KeepDelete_addr = 0x006FA8A8;
inline constexpr std::uint32_t KeepDelete_len = 4;
inline constexpr std::uint32_t KeepDelete_crc32 = 0x69B5D338;

inline constexpr std::uintptr_t CurrentSaveEntry_addr = 0x002FB900;
inline constexpr std::uint32_t CurrentSaveEntry_len = 4;
inline constexpr std::uint32_t CurrentSaveEntry_crc32 = 0xF83B0570;

inline constexpr std::uintptr_t GardenEntry_addr = 0x002FB354;
inline constexpr std::uint32_t GardenEntry_len = 4;
inline constexpr std::uint32_t GardenEntry_crc32 = 0x889FC869;

inline constexpr std::uintptr_t SlotIndexEntry_addr = 0x00309F18;
inline constexpr std::uint32_t SlotIndexEntry_len = 4;
inline constexpr std::uint32_t SlotIndexEntry_crc32 = 0xBBE700CB;

inline constexpr std::uintptr_t RoomManagerEntry_addr = 0x00308110;
inline constexpr std::uint32_t RoomManagerEntry_len = 4;
inline constexpr std::uint32_t RoomManagerEntry_crc32 = 0x07FD5F3E;

inline constexpr std::uintptr_t DailyUpdateEntry_addr = 0x0030C994;
inline constexpr std::uint32_t DailyUpdateEntry_len = 4;
inline constexpr std::uint32_t DailyUpdateEntry_crc32 = 0xF83B0570;

inline constexpr std::uintptr_t UseExitEntry_addr = 0x005B464C;
inline constexpr std::uint32_t UseExitEntry_len = 4;
inline constexpr std::uint32_t UseExitEntry_crc32 = 0x5809AA4E;

inline constexpr std::uintptr_t PocketLoadIcon_addr = 0x0026DB1C;
inline constexpr std::uint32_t PocketLoadIcon_len = 4;
inline constexpr std::uint32_t PocketLoadIcon_crc32 = 0xC31DF273;

inline constexpr std::uintptr_t PocketInit_addr = 0x002FCBB4;
inline constexpr std::uint32_t PocketInit_len = 4;
inline constexpr std::uint32_t PocketInit_crc32 = 0xF83B0570;

inline constexpr std::uintptr_t PocketEmpty_addr = 0x002FCB24;
inline constexpr std::uint32_t PocketEmpty_len = 4;
inline constexpr std::uint32_t PocketEmpty_crc32 = 0x76252507;

inline constexpr std::uintptr_t PocketFindEmpty_addr = 0x00723604;
inline constexpr std::uint32_t PocketFindEmpty_len = 4;
inline constexpr std::uint32_t PocketFindEmpty_crc32 = 0x2F862E7F;

inline constexpr std::uintptr_t PocketHidden_addr = 0x007238C0;
inline constexpr std::uint32_t PocketHidden_len = 4;
inline constexpr std::uint32_t PocketHidden_crc32 = 0x792D5167;

} // namespace gohan::game::generated
