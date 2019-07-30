
#ifndef H_BABYLON_TEXT
#define H_BABYLON_TEXT

#include <stdbool.h>
#include <stdio.h>


typedef struct babylon_text_t babylon_text_t;
typedef struct babylon_macro_t babylon_macro_t;

#ifdef __cplusplus
extern "C" {
#endif

   babylon_macro_t *babylon_macro_read (const char *filename);
   void babylon_macro_del (babylon_macro_t *m);


   babylon_text_t *babylon_text_new (void);
   babylon_text_t *babylon_text_transform (babylon_text_t *src,
                                           const babylon_macro_t *m);

   void babylon_text_del (babylon_text_t *b);

   bool babylon_text_read (babylon_text_t *b, const char *filename);
   bool babylon_text_write (babylon_text_t *b, FILE *outf);



#ifdef __cplusplus
};
#endif

#endif

