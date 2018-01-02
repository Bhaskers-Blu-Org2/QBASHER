// Copyright (c) Microsoft Corporation. All rights reserved.
// Licensed under the MIT license.

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <time.h>
#include <fcntl.h>
#include <math.h>
#ifdef WIN64
#include <windows.h>
#include <tchar.h>
#include <strsafe.h>
#include <Psapi.h>
#else
#include <errno.h>
#endif

#include "unicode.h"
#include "../shared/QBASHER_common_definitions.h"
#include "../shared/utility_nodeps.h"
#define PCRE2_CODE_UNIT_WIDTH 8
#include "../imported/pcre2/pcre2.h"

#include "substitutions.h"


#define MAX_DIRLEN 1000
#define SLASH '/'   // Change if it needs to be '\\'

void unload_substitution_rules(int num_substitution_rules, pcre2_code ***substitution_rules_regex, 
			       u_char ***substitution_rules_rhs, u_char **substitution_rules_rhs_has_operator) {
  int rule;
  if (substitution_rules_regex != NULL) {
    for (rule = 0; rule < num_substitution_rules; rule++) 
      if ((*substitution_rules_regex)[rule] != NULL) free((*substitution_rules_regex)[rule]);
    free(*substitution_rules_regex);
    *substitution_rules_regex = NULL;
  }

  if (substitution_rules_rhs != NULL) {
    for (rule = 0; rule < num_substitution_rules; rule++)
      if ((*substitution_rules_rhs)[rule] != NULL) free((*substitution_rules_rhs)[rule]);
    free(*substitution_rules_rhs);
    *substitution_rules_rhs = NULL;
  }
  if (substitution_rules_rhs_has_operator != NULL) {
    free(*substitution_rules_rhs_has_operator);
    *substitution_rules_rhs_has_operator = NULL;
  }
}


int load_substitution_rules(u_char *srfname, u_char *index_dir, u_char *language,
			    int *num_substitution_rules, pcre2_code ***substitution_rules_regex,
			    u_char ***substitution_rules_rhs, u_char **substitution_rules_rhs_has_operator, 
			    int debug) {
  // If qoenv->fname_substitution_rules is defined, then attempt to load that file, otherwise 
  // look for a file QBASH.substitution_rules_<language> in the index_dir
  // If not found, return 0, otherwise expect to find lines of the form <LHS> TAB <RHS>
  // in the file and return a count of the rules found, or a negative error code
  u_char *rulesfile_in_mem, *p, *line_start, *rhs_start;
  size_t dirlen, rulesfile_size, patlen, rhslen, error_offset;
  CROSS_PLATFORM_FILE_HANDLE H;
  HANDLE MH;
  int error_code = 0, lncnt = 0, rule, rules_with_operators_in_RHS = 0;

  if (srfname != NULL) {
    if (!exists((char *)srfname, "")) {
      return 0;  // ----------------------------------------------------->
    }
    if (debug > 0) printf("Loading substitution_rules from %s\n", srfname);
    fflush(stdout);
    rulesfile_in_mem = (u_char *)mmap_all_of(srfname, &rulesfile_size, FALSE, &H, &MH, &error_code);
    if (debug > 0) printf("Loaded substitution_rules from %s.  Error code is %d\n", srfname, error_code);
    fflush(stdout);

    if (error_code) return error_code;
  }
  else {
    char *tfn = NULL, katter[30];

    if (index_dir == NULL) {
      if (debug > 0) printf("  Substitutions file can't be loaded because index_dir isn't defined.\n");
      return 0;  // ----------------------------------------->
    }
    else  {
      // The index_dir is defined
      strcpy(katter, "QBASH.substitution_rules_");  // That string is 25 chars long
      strncpy(katter + 25, (char *)language, 2);  // Restrict language to 2-letter code
      katter[27] = 0;
      dirlen = strlen((char *)index_dir);
      tfn = (char *)malloc(dirlen + 30);
      if (tfn == NULL) {
	if (debug > 0) printf("  Substitutions file can't be loaded because malloc failed.\n");
	return 0;  // ----------------------------------------->
      }
      strcpy(tfn, (char *)index_dir);
      if (tfn[dirlen - 1] != SLASH) {
	tfn[dirlen++] = SLASH;
	tfn[dirlen] = 0;
      }
			
      if (!exists((char *)tfn, (char *)katter)) {
	if (debug > 0) printf("  Substitutions file %s%s doesn't exist.\n", tfn, katter);
	return 0;  // ----------------------------------------------------->
      }
    }
    // We've found the file.  Create it's name, Memory map it, count the lines, then create the mappings.
    if (debug > 0) printf("Loading substitution rules from %s%s\n", tfn, katter);
    strcpy(tfn + dirlen, katter);

    rulesfile_in_mem = (u_char *)mmap_all_of((u_char *)tfn, &rulesfile_size, FALSE, &H, &MH, &error_code);
    if (error_code) {
      if (debug > 0) printf("  Substitutions file %s can't be mmapped.\n", tfn);
      return error_code;  // ----------------------------------------------------->
    }
    free(tfn);
  }

  p = rulesfile_in_mem;
  while (p < rulesfile_in_mem + rulesfile_size) if (*p++ == '\n') lncnt++;  // This works for both Unix and Windows line termination.

  // Now malloc the memory
  *substitution_rules_regex = (pcre2_code **)malloc((lncnt + 1) * sizeof(pcre2_code *));

  if (*substitution_rules_regex == NULL) {
    unmmap_all_of(rulesfile_in_mem, H, MH, rulesfile_size);
    return -20079;
  }

  *substitution_rules_rhs = (u_char **)malloc((lncnt + 1) * sizeof(u_char *));
  if (*substitution_rules_rhs == NULL) {
    unmmap_all_of(rulesfile_in_mem, H, MH, rulesfile_size);
    free(*substitution_rules_regex);
    *substitution_rules_regex = NULL;
    return -20079;
  }

  *substitution_rules_rhs_has_operator = (u_char *)malloc((lncnt + 1) * sizeof(u_char));
  if (*substitution_rules_rhs_has_operator == NULL) {
    unmmap_all_of(rulesfile_in_mem, H, MH, rulesfile_size);
    free(*substitution_rules_rhs);
    free(*substitution_rules_regex);
    *substitution_rules_regex = NULL;
    *substitution_rules_rhs = NULL;
    return -20079;
  }

  for (rule = 0; rule < lncnt; rule++) {
    (*substitution_rules_regex)[rule] = NULL;
    (*substitution_rules_rhs)[rule] = NULL;
    (*substitution_rules_rhs_has_operator)[rule] = 0;
  }


  p = rulesfile_in_mem;
  for (rule = 0; rule < lncnt; rule++) {

    line_start = p;
    while (p < rulesfile_in_mem + rulesfile_size && *p != '\t' && *p != '\n') p++;
    if (*p == '\t') {
      // Yes, we have an LHS pattern starting at line_start.  Compile it.
      patlen = p - line_start;

      (*substitution_rules_regex)[rule] = pcre2_compile(line_start, patlen, PCRE2_UTF|PCRE2_CASELESS, &error_code, &error_offset, NULL);
      // Will be NULL if compilation failed.
      if ((*substitution_rules_regex)[rule] == NULL) {
	u_char errbuf[200];
	pcre2_get_error_message(error_code, errbuf, 199);
	if (debug >= 1) printf("Compile failed for rule starting with %s.  Error_code: %d: %s\n", line_start, error_code, errbuf);
      }

      p++;
      rhs_start = p;
      while (p < rulesfile_in_mem + rulesfile_size && *p != '\n') {
	if (*p == '\r' && *(p + 1) == '\n') break;   // Ignore CRs unless followed by LF
	p++;
      }
      rhslen = p - rhs_start;
      (*substitution_rules_rhs)[rule] = (u_char *)malloc(rhslen + 1);
      if ((*substitution_rules_rhs)[rule] == NULL) {
	error_code = -20079;
	break;
      }
			
      //memcpy((*substitution_rules_rhs)[rule], rhs_start, rhslen);
      utf8_lowering_ncopy((*substitution_rules_rhs)[rule], rhs_start, rhslen);
			
      (*substitution_rules_rhs)[rule][rhslen] = 0;
      if (strchr((char *)(*substitution_rules_rhs)[rule], '[') != NULL
	  || strchr((char *)(*substitution_rules_rhs)[rule], '"') != NULL) {
	(*substitution_rules_rhs_has_operator)[rule] = 1;
	rules_with_operators_in_RHS++;
      }
      else (*substitution_rules_rhs_has_operator)[rule] = 0;
      p++;
      if (debug >= 2) printf("RHS: %s\n", (*substitution_rules_rhs)[rule]);
    }
  }



  // Unload the memory-mapped file
  unmmap_all_of(rulesfile_in_mem, H, MH, rulesfile_size);

  if (error_code < 0) {
    printf("Error code is %d\n", error_code);
    for (rule = 0; rule < lncnt; rule++) {
      if ((*substitution_rules_regex)[rule] != NULL) free((*substitution_rules_regex)[rule]);
      if ((*substitution_rules_rhs)[rule] != NULL) free((*substitution_rules_rhs)[rule]);
    }

    free(*substitution_rules_rhs_has_operator);
    free(*substitution_rules_rhs);
    free(*substitution_rules_regex);
    *substitution_rules_rhs_has_operator = NULL;
    *substitution_rules_rhs = NULL;
    *substitution_rules_regex = NULL;


    *num_substitution_rules = 0;
    return error_code;
  }


  *num_substitution_rules = lncnt;
  if (debug >= 1) printf("Substitution rules loaded: %d\n", lncnt);  fflush(stdout);
  return lncnt;
}

#define INITIAL_SUBJECT_LEN_LIMIT 256  // If an input subject is longer than this no substitutions will occur.
#define MAX_SUBLINE MAX_RESULT_LEN  // This should be significantly larger than INITIAL_SUBJECT_LEN_LIMIT to allow for growth due to substitutions.

int apply_substitutions_rules_to_string(int num_substitution_rules, pcre2_code **substitution_rules_regex,
					u_char **substitution_rules_rhs,
					u_char *substitution_rules_rhs_has_operator,
					u_char *intext, BOOL avoid_operators_in_subject,
					BOOL avoid_operators_in_rule, int debug) {

  // Refuse to make substitutions if subject contains a '['
  // Attempt to apply ALL rules in file order, even if substitutions are made.  Two buffers are used,
  // referenced by sin and sout.  Substitutions are always attempted from sin to sout, and if a
  // substitution occurs sin and sout are swapped.
  // (intext is first copied into buf1 which starts of as sin, with sout referencing buf2

  int rule, num_subs, rules_matched = 0;
  u_char buf1[MAX_SUBLINE + 2], buf2[MAX_SUBLINE + 2], *sin = buf1, *sout = buf2, *t, *r, *w;
  size_t buflen, l;
  pcre2_match_data *p2md;

  if (num_substitution_rules == 0
      || substitution_rules_regex == NULL
      || substitution_rules_rhs == NULL) return 0;         // -----------------------------------------R>

  // copy intext to buf1 (up to MAX_SUBLINE characters)
  r = intext;
  w = buf1;
  if (avoid_operators_in_subject) {
    // This bit added to assist with substitution rules in Local search autocomplete where
    // user query may be prefixed with geotile expressions such as [x$5 x$7]
    t = (u_char *)strrchr((char *)intext, ']');  // Find last occurrence of a square bracket
    if (t != NULL) {
      // Copy over everything up to and including the last square bracket
      // and then do the substitutions after that.
      while (r <= t) *w++ = *r++;
      // r ends up pointing to the character beyond the last square bracket
    }
  }
  l = 0;  // Impose the length limit only on the part after ]	
  while (l <= INITIAL_SUBJECT_LEN_LIMIT && *r) {
    if (*r == '[' && avoid_operators_in_subject) return 0; // ------------------------------------------R>
		                            
    if (*r > 127 && *r < 160) {
      *w++ = ' ';  r++;   // Replace Windows-1252 punctuation chars with spaces.
    }
    else {
      *w++ = *r++;
    }
    l++;
  }
  if (*r) {
    if (debug > 1) printf("Substitutions skipped due to length > %d\n", INITIAL_SUBJECT_LEN_LIMIT);
    return (0);    // We scanned INITIAL_SUBJECT_LEN_LIMIT bytes and didn't get to the end. Don't do any substitutions.
  }
  *w = 0;  

  p2md = pcre2_match_data_create(10, NULL);  // Allow for up to ten different capturing sub-patterns

  if (p2md == NULL) return 0;   // Maybe this should be an error?

  if (debug >= 1)
    printf("apply_substitions_to_query_text(%s) called.  %d rules\n", intext, num_substitution_rules);


  // Try all the substitution rules
  for (rule = 0; rule < num_substitution_rules; rule++) {
    if (debug >= 2) printf("Rule %d\n", rule);
    buflen = MAX_SUBLINE + 1;  // Have to reset this each time, as unsuccessful substitute calls reset it.
    //  buflen sets the size of the output of each substitution.
    
    if (avoid_operators_in_rule && substitution_rules_rhs_has_operator[rule])
      continue; // ------------------------------C>
    if (substitution_rules_regex[rule] == NULL || substitution_rules_rhs[rule] == NULL) {
      if (0) printf("Left or right is NULL!\n");
      continue; // ------------------------------C>
    }
    if (debug >= 2) printf("Rule %d: RHS = '%s'.  Subject = %s\n", rule, substitution_rules_rhs[rule], sin);

    num_subs = multisub(substitution_rules_regex[rule], sin, strlen((char *)sin), 0,
			PCRE2_SUBSTITUTE_GLOBAL, p2md, NULL, substitution_rules_rhs[rule],
			strlen((char *)(substitution_rules_rhs[rule])), sout, &buflen);
    if (num_subs > 0) {
      if (debug >= 1) printf("Query substitution occurred: %s\n", sout);
      // Now switch in and out buffers
      t = sin;
      sin = sout;
      sout = t;
      rules_matched++;
    }
    else if (num_subs < 0 && debug >=1) {
      u_char errbuf[200];
      pcre2_get_error_message(num_subs, errbuf, 199);
      printf("Substitute failed for rule %d.  Error_code: %d: %s\n"
	     " - sin is %s, RHS is %s\n", rule, num_subs, errbuf, sin, substitution_rules_rhs[rule]);
    }
  }

  if (rules_matched > 0) strcpy((char *)intext, (char *)sin);
  if (debug >= 1) printf("Rules matched: %d\n", rules_matched);
  pcre2_match_data_free(p2md);

  return rules_matched;
}


int multisub(const pcre2_code *regex, PCRE2_SPTR sin, PCRE2_SIZE sinlen, PCRE2_SIZE startoff, uint32_t opts,
	     pcre2_match_data *p2md, pcre2_match_context *p2mc, PCRE2_SPTR rep, PCRE2_SIZE replen, PCRE2_UCHAR *obuf, PCRE2_SIZE *obuflen) {
  // We know (or suspect) that sin contains an operator AND that the replacement string contains one too.
  // We don't want to apply the regex to sections of sin within an operator section. E.g. within [ ... ] or " ... "
  // Therefore we have to divide the string into operator and non-operator sections and apply or not 
  // apply the rules as we go.
  u_char *section_start = (u_char *)sin, *obufupto = obuf, *obuf_end = obuf + *obuflen, *q;
  PCRE2_SIZE seclen;
  int num_subs = 0;
  // Loop over the sections
  while (*section_start) {
    // Look for an operator
    q = section_start;
    while (*q && *q != '[' && *q != '"') q++;
    seclen = q - section_start;
    if (seclen > 0) {
      PCRE2_SIZE lobufleft = obuf_end - obufupto + 1;
      if (0) printf("About to substitute.  Inlen = %d,  Outlen = %d\n", (int)seclen, (int)lobufleft);
      num_subs += pcre2_substitute(regex, section_start, seclen, 0,
				   PCRE2_SUBSTITUTE_GLOBAL, p2md, p2mc, rep,
				   strlen((char *)rep), obufupto, &lobufleft);
      obufupto += lobufleft;
    }
    if (*q == 0) break;  // ------------------------------------------------->

    // *q isn't null so it must be an operator.  Skip to the matching closing operator, copying into obuf
    u_char closer = *q;
    if (closer == '[') closer = ']';
    while (*q && *q != closer && obufupto < obuf_end) {
      *obufupto++ = *q++;
    }
    if (obufupto >= obuf_end) break; // ------------------------------------------------->

    *obufupto++ = closer;

    // q now points either to the end of the string or to a closing operator
    if (*q == 0) break;  // ------------------------------------------------->
    q++;
    section_start = q;
  }

  *obufupto = 0;  // Null terminate
  return num_subs;
}


BOOL re_match(u_char *needle, u_char *haystack, int pcre2_options, int debug) {
  // Return TRUE iff there are no PCRE2 errors and there is a non-empty match
  // for needle in haystack.  If debug > 0, any PCRE2 errors will be explained
  int error_code, rc;
  size_t error_offset;
  pcre2_code *compiled_pat;
  pcre2_match_data *p2md;
  u_char error_text[201];

  if (debug >= 2) printf("re_match called with (%s, %s)\n", needle, haystack);
  
  pcre2_options |= PCRE2_UTF;  // Always UTF-8!
  compiled_pat = pcre2_compile(needle, strlen((char *)needle), pcre2_options, &error_code, &error_offset, NULL);
  if (compiled_pat == NULL) {
    printf("Error: pcre2_compile error %d\n", error_code);
    return FALSE;
  }

  p2md = pcre2_match_data_create_from_pattern(compiled_pat, NULL);
  rc = pcre2_match(compiled_pat, haystack, strlen((char *)haystack), 0,
		   PCRE2_NOTEMPTY, p2md, NULL);
  if (rc < 0) {
    switch(rc) {
    case PCRE2_ERROR_NOMATCH:
      break;
    default: pcre2_get_error_message(rc, error_text, 100);
      if (debug >= 1) printf("Matching error %d: %s\n", rc, error_text);
      break;
    }
    pcre2_match_data_free(p2md);   
    pcre2_code_free(compiled_pat);
    return FALSE;
  }
  pcre2_match_data_free(p2md);   
  pcre2_code_free(compiled_pat); 
  return TRUE;
}
