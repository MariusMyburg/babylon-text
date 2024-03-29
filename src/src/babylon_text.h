
#ifndef H_BABYLON_TEXT
#define H_BABYLON_TEXT

#include <stdbool.h>
#include <stdio.h>


#define BABYLON_EPARAM        (-1)
#define BABYLON_EFREAD        (-2)

typedef struct babylon_text_t babylon_text_t;
typedef struct babylon_macro_t babylon_macro_t;

#ifdef __cplusplus
extern "C" {
#endif

   babylon_macro_t *babylon_macro_read (const char *filename);
   void babylon_macro_del (babylon_macro_t *bm);
   void babylon_macro_dump (babylon_macro_t *bm, FILE *outf);


   babylon_text_t *babylon_text_read (const char *filename);
   void babylon_text_del (babylon_text_t *b);

   babylon_text_t *babylon_text_transform (babylon_text_t *src,
                                           const babylon_macro_t *bm);

   bool babylon_text_write (babylon_text_t *b, FILE *outf);

   int babylon_text_errcode (babylon_text_t *b);
   const char *babylon_text_errmsg (babylon_text_t *b);


#ifdef __cplusplus
};
#endif

#endif

