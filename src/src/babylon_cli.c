#include <stdlib.h>
#include <stdio.h>

#include "babylon_text.h"

#define PROG_ERR(...)      do {\
   fprintf (stderr, "%s:%i:%s:", __FILE__, __LINE__, __func__);\
   fprintf (stderr, __VA_ARGS__);\
   fprintf (stderr, "\n");\
} while (0)

#define TEST_INPUT   ("test_input.bab")

int main (void)
{
   int ret = EXIT_FAILURE;

   babylon_text_t *b = NULL;

   printf ("Starting babylon processing\n");

   if (!(b = babylon_text_read (NULL, TEST_INPUT))) {
      PROG_ERR ("Failed to read input from file [%s]:%m\n", TEST_INPUT);
      goto errorexit;
   }

   if (babylon_text_errcode (b)) {
      PROG_ERR ("Error %i parsing [%s]:%s\n", babylon_text_errcode (b),
                                              TEST_INPUT,
                                              babylon_text_errmsg (b));
      goto errorexit;
   }

   ret = EXIT_SUCCESS;

errorexit:

   babylon_text_del (b);

   return ret;
}

