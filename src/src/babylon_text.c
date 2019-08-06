
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

static void node_dump (node_t *node, FILE *outf)
{
   if (!outf)
      outf = stdout;

   fprintf (outf, "%30s: %p\n", "START NODE", node);
   if (!node) {
      fprintf (outf, "%30s: %p\n", "END  NODE", node);
      return;
   }

   fprintf (outf, "%30s: %s\n",      "filename", node->filename);
   fprintf (outf, "%30s: %zu\n",     "line",     node->line);
   fprintf (outf, "%30s: %zu\n",     "charpos",  node->charpos);
   fprintf (outf, "%30s: %i\n",      "type",     node->type);
   fprintf (outf, "%30s: %s\n",      "text",     node->text);

   if (node->type == node_NODE) {
      size_t nkeys = 0;
      char **keys = NULL;
      size_t *keylens = NULL;

      nkeys = ds_hmap_keys (node->hmap, (void ***)&keys, &keylens);
      for (size_t i=0; i<nkeys; i++) {
         char *value = NULL;
         if (!(ds_hmap_get_str_str (node->hmap, keys[i], &value))) {
            LOG_ERR ("Failed to get key for [%s]\n", keys[i]);
         } else {
            fprintf (outf, "%30s => %s\n", keys[i], value);
         }
      }
      free (keys);
      free (keylens);
   }

   fprintf (outf, "----\n");
   for (size_t i=0; node->nodes && node->nodes[i]; i++) {
      node_dump (node->nodes[i], outf);
   }
   fprintf (outf, "%30s: %p\n", "END  NODE", node);
}

static void node_del (node_t *node)
{
   if (!node)
      return;

   free (node->filename);
   free (node->text);

   for (size_t i=0; node->nodes && node->nodes[i]; i++) {
      node_del (node->nodes[i]);
   }
   ds_array_del (node->nodes);

   if (node->hmap) {
      char **keys = NULL;
      size_t *keylens = NULL;

      size_t nkeys = ds_hmap_keys (node->hmap, (void ***)&keys, &keylens);
      for (size_t i=0; i<nkeys; i++) {
         char *value = NULL;
         ds_hmap_get_str_str (node->hmap, keys[i], &value);
         free (value);
      }

      free (keys);
      free (keylens);
      ds_hmap_del (node->hmap);
   }

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
      if (!(ret->hmap = ds_hmap_new (4))) {
         LOG_ERR ("Cannot create hashmap for node\n");
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

/* ************************************************************** */

// TODO: These functions make this module thread-unsafe. Refactor into a
// different module that maintains thread-safety
static void **g_instream = NULL;
void g_instream_cleanup (void)
{
   if (g_instream) {
      ds_array_del (g_instream);
      g_instream = NULL;
   }
}

static int get_next_char (FILE *inf, size_t *line, size_t *charpos)
{
   if (!g_instream) {
      if (!(g_instream = ds_array_new ())) {
         LOG_ERR ("Fatal error creating array\n");
         return 0;
      }
      atexit (g_instream_cleanup);
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
      atexit (g_instream_cleanup);
   }

   if (!(ds_array_ins_tail (&g_instream, (void *)(intptr_t)c))) {
      LOG_ERR ("Fatal error creating array\n");
      return;
   }
}

static char *get_next_word (FILE *inf, const char *extra_delims,
                            int *delim_dst,
                            size_t *line, size_t *charpos)
{
   bool error = true;
   char *ret = NULL;

   int c = 0;
   bool inq = false;

   *delim_dst = EOF;

   while ((c = get_next_char (inf, line, charpos)) != EOF) {
      char tmp[2];

      if (c=='\\') {
         if ((c = get_next_char (inf, line, charpos)) == EOF)
            break;
      }

      if (c=='"') {
         inq = !inq;
         continue;
      }

      if (!inq) {
         if (isspace (c) || strchr (extra_delims, (char)c)) {
            *delim_dst = (char)c;
            break;
         }
      }

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

static char *get_next_line (FILE *inf, size_t *line, size_t *charpos)
{
   bool error = true;
   char *ret = NULL;

   char tmp[2] = { 0, 0 };
   int c = 0;

   if (!inf || !line || !charpos)
      goto errorexit;

   while ((c = get_next_char (inf, line, charpos))!=EOF) {
      tmp[0] = c;
      if (!(ds_str_append (&ret, tmp, NULL))) {
         LOG_ERR ("OOM\n");
         goto errorexit;
      }
      if (c == '\n')
         break;
   }

   error = false;

errorexit:
   if (error) {
      free (ret);
      ret = NULL;
   }

   return ret;
}

static bool read_nv (FILE *inf, char **name, char **value,
                     size_t *line, size_t *charpos)
{
   long stream_pos = ftell (inf);
   size_t stream_line = *line;
   size_t stream_charpos = *charpos;

   int delim = 0;
   char *l_name = NULL,
        *l_value = NULL;

   if ((l_name = get_next_word (inf, "#[]=", &delim, line, charpos))) {
      if ((l_value = get_next_word (inf, "#[]", &delim, line, charpos))) {
         *name = l_name;
         *value = l_value;
         return true;
      }
   }

   free (l_name);
   free (l_value);

   fseek (inf, SEEK_SET, stream_pos);
   *line = stream_line;
   *charpos = stream_charpos;
   return false;
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
   int delim = 0;

   char *name = NULL,
        *value = NULL;

   // Discard the first character
   int c = get_next_char (inf, line, charpos);
   c = c;

   if (!(text = get_next_word (inf, "#[]", &delim, line, charpos))) {
      LOG_ERR ("Failed to read tagname\n");
      goto errorexit;
   }

   if (!(ret = node_new (filename, node_NODE, text, *line, *charpos))) {
      LOG_ERR ("Failed to create return node [%s]\n", text);
      goto errorexit;
   }

   while ((read_nv (inf, &name, &value, line, charpos))) {
      if (!(ds_hmap_set_str_str (ret->hmap, name, value))) {
         free (name);
         free (value);
         goto errorexit;
      }
      free (name);
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

   int delim = 0;

   if (!(text = get_next_word (inf, "#[]", &delim, line, charpos))) {
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
   char *directive = NULL;
   char *fname = NULL;
   int delim = 0;
   node_t *ret = NULL;

   filename = filename;
   // Discard the first character
   get_next_char (inf, line, charpos);

   if (!(directive = get_next_word (inf, "[]", &delim, line, charpos))) {
      LOG_ERR ("Failed to get directive after #\n");
      goto errorexit;
   }

   LOG_ERR ("Running directive [%s]\n", directive);
   if ((strcmp (directive, "include"))==0) {
      if (!(fname = get_next_word (inf, "[]", &delim, line, charpos))) {
         LOG_ERR ("Failed to include directive\n");
         goto errorexit;
      }

      LOG_ERR ("Loading [%s]\n", fname);
      ret = node_readfile (fname);
   }

errorexit:
   free (directive);
   free (fname);
   return ret;
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
         if (!(cur = read_text (inf, filename, line, charpos)))
            goto errorexit;

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

bool babylon_text_write (babylon_text_t *b, FILE *outf)
{
   if (!outf)
      outf = stdout;

   if (!b) {
      LOG_ERR ("NULL object passed to function\n");
      return false;
   }

   node_dump (b->root, outf);
   return true;
}

int babylon_text_errcode (babylon_text_t *b)
{
   return b ? b->errcode : BABYLON_EPARAM;
}

const char *babylon_text_errmsg (babylon_text_t *b)
{
   return b ? b->errmsg : "bad param";
}

/* ************************************************************** */

struct babylon_macro_t {
   char *filename;
   ds_hmap_t *macros;
};

struct macro_t {
   char *filename;
   char *name;
   char *body;
   size_t line;
   size_t charpos;
};

static void macro_del (struct macro_t *m)
{
   if (!m)
      return;

   free (m->filename);
   free (m->name);
   free (m->body);
   free (m);
}

static struct macro_t *macro_new (const char *filename,
                                  const char *name, const char *body,
                                  size_t line, size_t charpos)
{
   bool error = true;
   struct macro_t *ret = NULL;

   if (!(ret = malloc (sizeof *ret))) {
      LOG_ERR ("OOM error (macro storage)\n");
      goto errorexit;
   }

   memset (ret, 0, sizeof *ret);

   ret->filename = ds_str_dup (filename);
   ret->name = ds_str_dup (name);
   ret->body = ds_str_dup (body);
   ret->line = line;
   ret->charpos = charpos;

   if (!ret->filename || !ret->name || !ret->body) {
      LOG_ERR ("OOM error macro fields\n");
      goto errorexit;
   }

   error = false;

errorexit:
   if (error) {
      macro_del (ret);
      ret = NULL;
   }

   return ret;
}

static void macro_dump (struct macro_t *m, FILE *outf)
{
   if (!outf)
      outf = stdout;

   if (!m) {
      fprintf (outf, "Error: NULL Macro object!\n");
      return;
   }

   fprintf (outf, "   Macro Name:      [%s]\n", m->name);
   fprintf (outf, "   From:            [%s:%zu:%zu]\n", m->filename,
                                                        m->line,
                                                        m->charpos);
   fprintf (outf, "   Macro body:      [%s]\n", m->body);
}

void babylon_macro_dump (babylon_macro_t *bm, FILE *outf)
{
   if (!outf)
      outf = stdout;

   if (!bm) {
      fprintf (outf, "Error: NULL macro object.\n");
      return;
   }

   char **keys = NULL;
   size_t *keylens = NULL;
   size_t nkeys = ds_hmap_keys (bm->macros, (void *)&keys, &keylens);

   fprintf (outf, "--------------------------\n");
   fprintf (outf, "Filename:          %s\n", bm->filename);
   fprintf (outf, "Number of macros:  %zu\n", nkeys);
   for (size_t i=0; i<nkeys; i++) {
      struct macro_t *value = NULL;
      size_t valuelen = 0;
      if (!(ds_hmap_get_str_ptr (bm->macros, keys[i], (void **)&value,
                                                      &valuelen))) {
         LOG_ERR ("Internal error: Failed to get value for [%s]\n", keys[i]);
      } else {
         macro_dump (value, outf);
      }
   }
   fprintf (outf, "--------------------------\n");

   free (keys);
   free (keylens);
}

babylon_macro_t *babylon_macro_read (const char *filename)
{
   bool error = true;
   babylon_macro_t *ret = NULL;

   char *input = NULL;
   char *name = NULL;
   char *body = NULL;

   FILE *inf = NULL;

   size_t line = 0,
          charpos = 0,
          p_line = 0,
          p_charpos = 0;

   if (!(ret = malloc (sizeof *ret))) {
      LOG_ERR ("OOM\n");
      goto errorexit;
   }

   memset (ret, 0, sizeof *ret);

   if (!(ret->filename = ds_str_dup (filename))) {
      LOG_ERR ("OOM\n");
      goto errorexit;
   }

   if (!(inf = fopen (filename, "r"))) {
      LOG_ERR ("Failed to open file [%s] for reading: %m\n", filename);
      goto errorexit;
   }

   if (!(ret->macros = ds_hmap_new (10))) {
      LOG_ERR ("Failed to create hashmap for macros\n");
      goto errorexit;
   }

   while ((input = get_next_line (inf, &line, &charpos))!=NULL) {
      // The first non-empty line signifies the start of a macro and
      // contains the name of the macro.
      ds_str_trim (input);
      if (!input[0]) {
         free (input);
         continue;
      }

      free (name);
      name = input;

      free (body);
      body = NULL;

      p_line = line;
      p_charpos = charpos;

      // Repeatedly retrieve lines until we get an empty one
      while ((input = get_next_line (inf, &line, &charpos))) {
         if (!input[0] || input[0]=='\n' || (input[0]=='\r' && input[1]=='\n'))
            break;

         if (!(ds_str_append (&body, input, NULL))) {
            LOG_ERR ("%s:%zu: Macro [%s] Out of memory error\n",
                      filename, line, name);
            goto errorexit;
         }
         free (input);
      }
      free (input);

      if (!body)
         body = ds_str_dup ("");

      struct macro_t *new_macro = macro_new (filename, name, body,
                                             p_line, p_charpos);
      if (!new_macro) {
         LOG_ERR ("%s:%zu:%zu Failed to create new macro\n", filename,
                                                             line,
                                                             charpos);
         goto errorexit;
      }

      if (!(ds_hmap_set_str_ptr (ret->macros, name, new_macro,
                                                    sizeof new_macro))) {
         LOG_ERR ("%s:%zu: Macro [%s]: Failed to store body [%s]\n",
                     filename, line, name, body);
         macro_del (new_macro);
         goto errorexit;
      }
   }

   error = false;

errorexit:
   if (inf)
      fclose (inf);

   free (input);
   free (name);
   free (body);

   if (error) {
      babylon_macro_del (ret);
      ret = NULL;
   }

   return ret;
}

void babylon_macro_del (babylon_macro_t *bm)
{
   if (!bm)
      return;

   free (bm->filename);
   char **keys = NULL;
   size_t *keylens = NULL;
   size_t nkeys = ds_hmap_keys (bm->macros, (void ***)&keys, &keylens);

   for (size_t i=0; i<nkeys; i++) {
      struct macro_t *value = NULL;
      size_t valuelen = 0;
      if (!(ds_hmap_get_str_ptr (bm->macros, keys[i], (void **)&value,
                                                      &valuelen))) {
         LOG_ERR ("Internal error: Failed to get value for [%s]\n", keys[i]);
      }
      macro_del (value);
   }
   free (keylens);
   free (keys);

   ds_hmap_del (bm->macros);
   free (bm);
}


