
#include "babylon_text.h"

#include "ds_array.h"
#include "ds_hmap.h"


enum node_type_t {
   node_NODE,
   node_VALUE
};

typedef struct node_t node_t;

struct node_t {
   // Token location
   char *filename;
   size_t line;
   size_t charpos;

   // The actual token.
   // Each node is either a pointer to another tree or a value. If it's a
   // NODE type then 'text' contains the tag value, otherwise 'text'
   // contains the text of the token and the remaining fields are ignored.
   enum node_type_t type;
   char *text;
   ds_hmap_t *hmap;
   size_t nnodes;
   void **nodes;
};

struct babylon_text_t {
   size_t nnodes;
   void **nodes;
};

