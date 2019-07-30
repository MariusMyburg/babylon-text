
#include <stdlib.h>

#include "babylon_text.h"

#include "ds_array.h"
#include "ds_str.h"
#include "ds_hmap.h"

#define LOG_ERR(...)      do {\
   fprintf (stderr, "%s:%i:%s:", __FILE__, __LINE__, __func__);\
   fprintf (stderr, __VA_ARGS__);\
   fprintf (stderr, "\n");\
} while (0)


/* ************************************************************** */

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

static node_t *node_readfile (const char *filename)
{
   bool error = true;
   FILE *inf = NULL;

   node_t *ret = NULL;

   if (!(inf = fopen (filename, "r"))) {
      LOG_ERR ("Failed to open file [%s]:%m\n", filename);
      goto errorexit;
   }

   error = false;

errorexit:
   if (error) {
      node_del (ret);
      ret = NULL;
   }
   if (inf)
      fclose (inf);

   return ret;
}

/* ************************************************************** */

struct babylon_text_t {
   node_t *root;

   int errcode;
   char *errmsg;
};

void babylon_text_error (babylon_text_t *b, int errcode)
{
   static const struct {
      int         errcode;
      const char *errmsg;
   } errors[] = {
      { BABYLON_EPARAM, "Bad parameter"      },
      { BABYLON_EFREAD, "Input-file error"   },
   };

   char *tmp = NULL;

   if (!b)
      return;

   for (size_t i=0; i<sizeof errors/sizeof errors[0]; i++) {
      if (errors[i].errcode == errcode) {
         if (!(tmp = ds_str_dup (errors[i].errmsg))) {
            LOG_ERR ("Fatal error: OOM\n");
            return;
         }
      }
   }

   if (!tmp)
      ds_str_printf (&tmp, "Unknown error [%i]", errcode);

   if (!tmp) {
      LOG_ERR ("Fatal error: OOM\n");
      return;
   }

   free (b->errmsg);
   b->errmsg = tmp;
}

babylon_text_t *babylon_text_read (const char *filename)
{
   babylon_text_t *ret = NULL;

   if (!(ret = malloc (sizeof *ret))) {
      LOG_ERR ("OOM\n");
      goto errorexit;
   }

   LOG_ERR ("Created\n");

   memset (ret, 0, sizeof *ret);
   ret->errcode = 0;
   ret->errmsg = ds_str_dup ("Success");
   if (!(ret->root = node_readfile (filename))) {
      LOG_ERR ("Failed to read file [%s]:%m\n", filename);
      babylon_text_error (ret, BABYLON_EFREAD);
      goto errorexit;
   }

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


