
#include <stdlib.h>
#include <ctype.h>
#include <stdint.h>

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
   void **nodes;
};

static void node_dump (node_t *node)
{
   printf ("%30s: %p\n", "START NODE", node);
   if (!node) {
      printf ("%30s: %p\n", "END  NODE", node);
      return;
   }

   printf ("%30s: %s\n",      "filename", node->filename);
   printf ("%30s: %zu\n",     "line",     node->line);
   printf ("%30s: %zu\n",     "charpos",  node->charpos);
   printf ("%30s: %i\n",      "type",     node->type);
   printf ("%30s: %s\n",      "text",     node->text);
   printf ("----\n");
   for (size_t i=0; node->nodes && node->nodes[i]; i++) {
      node_dump (node->nodes[i]);
   }
   printf ("%30s: %p\n", "END  NODE", node);
}

static void node_del (node_t *node)
{
   if (!node)
      return;

   free (node->filename);
   free (node->text);
   ds_hmap_del (node->hmap); // TODO free the values

   for (size_t i=0; node->nodes && node->nodes[i]; i++) {
      node_del (node->nodes[i]);
   }
   ds_array_del (node->nodes);

   free (node);
}

static node_t *node_new (const char *filename, enum node_type_t type,
                         const char *text, size_t line, size_t charpos)
{
   bool error = true;
   node_t *ret = NULL;

   if (!(ret = malloc (sizeof *ret))) {
      LOG_ERR ("OOM\n");
      goto errorexit;
   }

   memset (ret, 0, sizeof *ret);
   ret->filename = ds_str_dup (filename);
   ret->text = ds_str_dup (text);
   ret->type = type;
   ret->line = line;
   ret->charpos = charpos;

   if (type == node_NODE) {
      if (!(ret->nodes = ds_array_new ())) {
         LOG_ERR ("Cannot create new array of children for node\n");
         goto errorexit;
      }
   }

   if (!ret->filename || !ret->text) {
      LOG_ERR ("OOM\n");
      goto errorexit;
   }

   error = false;

errorexit:

   if (error) {
      node_del (ret);
      ret = NULL;
   }

   return ret;
}

// TODO: These functions make this module thread-unsafe. Refactor into a
// different module that maintains thread-safety
static void **g_instream = NULL;

static int get_next_char (FILE *inf, size_t *line, size_t *charpos)
{
   if (!g_instream) {
      if (!(g_instream = ds_array_new ())) {
         LOG_ERR ("Fatal error creating array\n");
         return 0;
      }
   }

   if (ds_array_length (g_instream) > 0) {
      return (int) (intptr_t) ds_array_remove_tail (&g_instream);
   }

   int ret = fgetc (inf);

   (*charpos) += 1;

   if (ret == '\n') {
      (*line) += 1;
      (*charpos) = 0;
   }

   return ret;
}

static void unget_char (int c, FILE *inf)
{
   inf = inf;

   if (!g_instream) {
      if (!(g_instream = ds_array_new ())) {
         LOG_ERR ("Fatal error creating array\n");
         return;
      }
   }

   if (!(ds_array_ins_tail (&g_instream, (void *)(intptr_t)c))) {
      LOG_ERR ("Fatal error creating array\n");
      return;
   }
}

static char *get_next_word (FILE *inf, const char *filename,
                            size_t *line, size_t *charpos)
{
   bool error = true;
   char *ret = NULL;

   int c = 0;

   size_t o_line = *line,
          o_charpos = *charpos;

   while ((c = get_next_char (inf, line, charpos)) != EOF) {
      char tmp[2];

      if (isspace (c) || c=='#' || c=='[' || c==']')
         break;

      if (c=='\\')
         if (c == EOF)
            break;

      tmp[0] = c;
      tmp[1] = 0;
      if (!(ds_str_append (&ret, tmp, NULL))) {
         goto errorexit;
      }
   }

   if (!ret)
      goto errorexit;

   error = false;

errorexit:


   if (error) {
      free (ret);
      ret = NULL;
   }

   return ret;
}


/* ***************************************************************** */

static node_t *node_readfile (const char *filename);
static node_t *node_read_next (node_t *parent,
                               FILE *inf, const char *filename,
                               size_t *line, size_t *charpos);

static node_t *read_tree (FILE *inf, const char *filename,
                          size_t *line, size_t *charpos)
{
   bool error = true;
   node_t *ret = NULL;

   char *text = NULL;

   // Discard the first character
   int c = get_next_char (inf, line, charpos);

   if (!(text = get_next_word (inf, filename, line, charpos))) {
      LOG_ERR ("Failed to read tagname\n");
      goto errorexit;
   }

   if (!(ret = node_new (filename, node_NODE, text, *line, *charpos))) {
      LOG_ERR ("Failed to create return node [%s]\n", text);
      goto errorexit;
   }

   if (!(node_read_next (ret, inf, filename, line, charpos))) {
      LOG_ERR ("Failed to append tree to node\n");
      goto errorexit;
   }

   error = false;

errorexit:

   free (text);

   if (error) {
      node_del (ret);
      ret = NULL;
   }

   return ret;
}

static node_t *read_text (FILE *inf, const char *filename,
                          size_t *line, size_t *charpos)
{
   node_t *ret = NULL;
   char *text = NULL;

   size_t o_line = *line,
          o_charpos = *charpos;

   if (!(text = get_next_word (inf, filename, line, charpos))) {
      LOG_ERR ("OOM\n");
      goto errorexit;
   }

   if (!(ret = node_new (filename, node_VALUE, text, o_line, o_charpos))) {
      LOG_ERR ("Failure creating new node\n");
      goto errorexit;
   }

errorexit:
   free (text);
   return ret;
}

static node_t *read_directive (FILE *inf, const char *filename,
                               size_t *line, size_t *charpos)
{
   // Discard the first character
   get_next_char (inf, line, charpos);

   return NULL;
}

static node_t *node_read_next (node_t *parent,
                               FILE *inf, const char *filename,
                               size_t *line, size_t *charpos)
{
   bool error = true;
   node_t *ret = NULL,
          *cur = NULL;

   int c = 0;

   if (!parent) {
      if (!(ret = node_new (filename, node_NODE, "root", *line, *charpos))) {
         LOG_ERR ("OOM\n");
         goto errorexit;
      }
   }

   while ((c = get_next_char (inf, line, charpos)) != EOF) {

      node_t *tmp = parent ? parent : ret;

      if ((isspace (c)))
         continue;

      if (c == ']')
         break;

      unget_char (c, inf);

      cur = NULL;

      if (c == '[')
         cur = read_tree (inf, filename, line, charpos);

      if (c == '#')
         cur = read_directive (inf, filename, line, charpos);

      if (!cur)
         cur = read_text (inf, filename, line, charpos);

      if (!cur)
         break;

      if (!(ds_array_ins_tail (&tmp->nodes, cur))) {
         LOG_ERR ("Failed to append to array\n");
         goto errorexit;
      }
   }

   error = false;

errorexit:

   if (error) {
      node_del (ret);
      return NULL;
   }

   return parent ? parent : ret;
}

static node_t *node_readfile (const char *filename)
{
   bool error = true;
   FILE *inf = NULL;

   node_t *ret = NULL;

   size_t line = 0,
          charpos = 0;

   if (!(inf = fopen (filename, "r"))) {
      LOG_ERR ("Failed to open file [%s]:%m\n", filename);
      goto errorexit;
   }

   if (!(ret = node_read_next (NULL, inf, filename, &line, &charpos))) {
      LOG_ERR ("Failed to read a node\n");
      goto errorexit;
   }

   node_dump (ret);

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


