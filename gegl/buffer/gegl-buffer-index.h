/* File format building blocks 

GeglBuffer on disk representation
=================================

*/

#define GEGL_MAGIC             {'G','E','G','L'}
#define GEGL_FLAG_HEADER       1
#define GEGL_FLAG_TILE         2
#define GEGL_FLAG_TILE_IS_FREE 4
#define GEGL_FLAG_TILE_FREE    (GEGL_FLAG_TILE|GEGL_FLAG_TILE_IS_FREE)


/*
 * This header is the first 256 bytes of the GEGL buffer.
 */
typedef struct {
  gchar     magic[4];    /* - a 4 byte identifier */
  guint32   flags;
  guint64   next;        /* offset to next GeglBufferBlock */

  guint32   tile_width;
  guint32   tile_height;
  guint16   bytes_per_pixel;

  gchar     description[64]; /* GEGL stores the string of the babl format
                              * here, as well as after the \0 a debug string
                              * describing the buffer.
                              */
  gint32    x;               /* indication of bounding box for tiles stored. */
  gint32    y;               /* this isn't really needed as each GeglBuffer as */
  guint32   width;           /* represented on disk doesn't really have any */
  guint32   height;          /* dimension restriction. */

  guint32   entry_count;     /* for integrity check. */
  gint32    padding[36];     /* Pad the structure to be 256 bytes long */
} GeglBufferHeader;


/* The GeglBuffer index is written to the file as a linked list with encoded
 * offsets. Each element in the list has a GeglBufferBlock header, GeglBufferHeader
 * abuses the length field to construct a magic marker to recognise the file.
 */

typedef struct {
  guint32  length; /* the length of this block */
  guint32  flags;  /* flags (can be used to handle different block types
                    * differently
                    */
  guint64  next;   /*next block element in file */
} GeglBufferBlock;

/* The header is followed by elements describing tiles stored in the swap,
 */
typedef struct {
  GeglBufferBlock blockdef; /* The block definition for this tile entry   */
  gint32  x;                /* upperleft of tile % tile_width coordinates */
  gint32  y;           

  gint32  z;                /* mipmap subdivision level of tile (0=100%)  */
  guint64 offset;           /* offset into file for this tile             */
  guint32 padding[23];
} GeglBufferTile;

/* A convenience union to allow quick and simple casting */
typedef union {
  guint32           length;
  GeglBufferBlock   block;
  GeglBufferHeader  def;
  GeglBufferTile    tile;
} GeglBufferItem;

#define struct_check_padding(type, size) \
  if (sizeof (type) != size) \
    {\
      g_warning ("%s %s is %i bytes, should be %i padding is off by: %i bytes %i ints", G_STRFUNC, #type,\
         (int) sizeof (type),\
         size,\
         (int) sizeof (type) - size,\
         (int)(sizeof (type) - size) / 4);\
      return;\
    }
#define GEGL_BUFFER_STRUCT_CHECK_PADDING \
  {struct_check_padding (GeglBufferBlock, 16);\
  struct_check_padding (GeglBufferHeader,   256);\
  struct_check_padding (GeglBufferTile, 128);}
#define GEGL_BUFFER_SANITY {static gboolean done=FALSE;if(!done){GEGL_BUFFER_STRUCT_CHECK_PADDING;done=TRUE;}}

