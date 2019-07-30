
#include <stdlib.h>

#include "babylon_text.h"

#include "ds_array.h"
#include "ds_hmap.h"

#define LOG_ERR(...)      do {\
   fprintf (stderr, "%s:%i:%s:", __FILE__, __LINE__, __func__);\
   fprintf (stderr, __VA_ARGS__);\
   fprintf (stderr, "\n");\
} while (0)


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
   node_t *root;

   int errcode;
   char *errmsg;
};

static void node_del (node_t *node)
{
   if (!node)
      return;

   free (node->filename);
   free (node->text);
   ds_hmap_del (node->hmap); // TODO free the values

   for (size_t i=0; node->nodes[i]; i++) {
      node_del (node->nodes[i]);
   }
   ds_array_del (node->nodes);
}

babylon_text_t *babylon_text_read (const babylon_text_t *src,
                                   const char *filename)
{
   babylon_text_t *ret = NULL;

   if (!(ret = malloc (sizeof *ret))) {
      LOG_ERR ("OOM\n");
      goto errorexit;
   }

   LOG_ERR ("Created\n");

   memset (ret, 0, sizeof *ret);
   ret->errcode = -1;

errorexit:
   return ret;
}

void babylon_text_del (babylon_text_t *b)
{
   if (!b)
      return;

   free (b->errmsg);
   node_del (b->root);
   free (b);
}

int babylon_text_errcode (babylon_text_t *b)
{
   return b ? b->errcode : BABYLON_EPARAM;
}

const char *babylon_text_errmsg (babylon_text_t *b)
{
   return b ? b->errmsg : "bad param";
}


