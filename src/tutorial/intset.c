/*
 * src/tutorial/intset.c
 *
 ******************************************************************************
  This file contains routines that can be bound to a Postgres backend and
  called by the backend in the process of processing queries.  The calling
  format for these routines is dictated by Postgres architecture.
******************************************************************************/

#include "postgres.h"
#include <string.h>
#include <stdlib.h>
#include <errno.h>

#include "fmgr.h"
#include "libpq/pqformat.h"		/* needed for send/recv functions */
#include "../include/utils/builtins.h"

PG_MODULE_MAGIC;

typedef struct {
	int32 length;
	int data[FLEXIBLE_ARRAY_MEMBER];
} Intset;


/* string process function*/
typedef enum {
    CONVERT_SUCCESS,
    CONVERT_OVERFLOW,
    CONVERT_UNDERFOW,
    CONVERT_INVONVERTIBLE
} convert_result;
convert_result convert_str2int(int *out, char *s);
convert_result get_next_number(char *s, int start, int end, int *out);

/*       hash set function     */
typedef struct _hash_node{
    int value;
    int exist;
    struct _hash_node *next;
} hash_node;

int hash_function(int x);
hash_node *init_hash_set();
hash_node *found_value_or_not(hash_node *hash_int_set, int value);
hash_node *insert_value(hash_node *hash_int_set, int x, int *number_of_value);
void value_print(hash_node *hash_int_set, int x);

/* change hash int set to int array */
int *hash2arry(hash_node *hash_int_set, int number_of_value);


/* intset in function */
PG_FUNCTION_INFO_V1(intset_in);

Datum
intset_in(PG_FUNCTION_ARGS)
{
	// hash_set init
    int number_of_value = 0;
    hash_node *hash_int_set = init_hash_set();

	char *s = PG_GETARG_CSTRING(0);
    int i;
    int string_length = strlen(s);

	int empty_flag = 0;
    int start = 0;
    int next_start = 0;
    int assign_flag = 0;
    int end;
	int result;

	int *array_int;


    if (s[0] != '{' || s[string_length - 1] != '}') {
        ereport(ERROR,
				(errcode(ERRCODE_FDW_INVALID_STRING_FORMAT),
				 errmsg("invalid input syntax for intset: \"%s\"",
						s)));
    }
	/* string is empty */
    if (string_length == 2) {
		Intset *t = (Intset *) palloc(VARHDRSZ);
		SET_VARSIZE(t, VARHDRSZ);
        PG_RETURN_POINTER(t);
    }

    while (next_start < string_length - 1) {
        start = next_start + 1;
        assign_flag = 0;
        for (i = start; i < string_length - 1; i++) {
            if (s[i] == ',') {
                next_start = i;
                assign_flag = 1;
                break;
            }
        }
        if (!assign_flag) {
            if (start == 1) empty_flag = 1;
            next_start = string_length - 1;
        }
        end = next_start - 1;
        while (end >= start && s[end] == ' ') {
            end--;
        }
        if (end < start) {
            if (!empty_flag) {
                ereport(ERROR,
				(errcode(ERRCODE_FDW_INVALID_STRING_FORMAT),
				 errmsg("invalid input syntax for intset: \"%s\"", s)));
            } else {
                Intset *t = (Intset *) palloc(VARHDRSZ);
				SET_VARSIZE(t, VARHDRSZ);
        		PG_RETURN_POINTER(t);
				
            }
            
        }
        
        if (get_next_number(s, start, end, &result) == CONVERT_SUCCESS) {
            insert_value(hash_int_set, result, &number_of_value);
        } else {
            ereport(ERROR,
				(errcode(ERRCODE_FDW_INVALID_STRING_FORMAT),
				 errmsg("invalid convert input syntax for intset: \"%s\"",
						s)));
        }
    }
    
    array_int = hash2arry(hash_int_set, number_of_value);
    // for (i = 0; i < number_of_value; i++) {
    //     printf("%d\n", array_int[i]);s
    // }

	Intset *t = (Intset *) palloc(VARHDRSZ + number_of_value * sizeof(int));
	SET_VARSIZE(t, VARHDRSZ + number_of_value * sizeof(int));
	memcpy(t -> data, array_int, number_of_value * sizeof(int));
    PG_RETURN_POINTER(t);
}


/* intset out function */
PG_FUNCTION_INFO_V1(intset_out);

Datum
intset_out(PG_FUNCTION_ARGS)
{

	Intset *intset = (Intset *) PG_GETARG_POINTER(0);
	char *result;
	int number_of_value = (intset -> length) / 16  - 1; 
	int i;
	if (number_of_value == 0) {
		result = "{ }";
		PG_RETURN_CSTRING(result);
	}
	for (i = 0; i < number_of_value; i++) {
		if (i == 0) {
			result = psprintf("{%d", intset -> data[i]);
		} else {
			result = psprintf("%s,%d", result, intset -> data[i]);
		}
	}
	result = psprintf("%s}", result);
	PG_RETURN_CSTRING(result);
}




/* 		string process  	*/
convert_result convert_str2int(int *out, char *s) {
    char *errstr;
    long l = strtoll(s, &errstr, 10);
    if (l > 2147483647 || (errno == ERANGE && l == 2147483647))
        return CONVERT_OVERFLOW;
    if (l < INT32_MIN || (errno == ERANGE && l == INT32_MIN))
        return CONVERT_UNDERFOW;
    if (*errstr != '\0')
        return CONVERT_INVONVERTIBLE;
    *out = (int)l;
    return CONVERT_SUCCESS;
}

convert_result get_next_number(char *s, int start, int end, int *out) {
    char *sub_s = malloc((end - start + 2) * sizeof(char));
    memcpy(sub_s, &s[start], end - start + 1);
    sub_s[ end - start + 1] = '\0';
    convert_result r = convert_str2int(out, sub_s);
    free(sub_s);
    return r;
}
/*       end      */



/*       hash set function     */

int hash_function(int x) {
    if (x < 0) return x % 1024 + 1024;
    return x % 1024;
}

hash_node *init_hash_set() {
    hash_node *x = malloc(1024 * sizeof(hash_node));
    int i;
    for (i = 0; i < 1024; i++) {
        x[i].value = 0;
        x[i].exist = 0;
        x[i].next = NULL;
    }
    return x;
}

hash_node *found_value_or_not(hash_node *hash_int_set, int value) {
    while (hash_int_set -> next) {
        if (hash_int_set -> value == value) {
            
            return hash_int_set;
        }
        hash_int_set = hash_int_set -> next;
    }
    return hash_int_set;
}

hash_node *insert_value(hash_node *hash_int_set, int x, int *number_of_value) {
    int value = hash_function(x);
    if (!hash_int_set[value].exist) {
        hash_int_set[value].value = x;
        hash_int_set[value].exist = 1;
        *number_of_value = *number_of_value + 1;
    } else {
        hash_node *get_result = found_value_or_not(&hash_int_set[value], x);
        if (get_result -> value == x) {
            return hash_int_set;
        } else {
            hash_node *new_node = malloc(sizeof(hash_node));
            new_node -> value = x;
            new_node -> exist = 1;
            new_node -> next = NULL;
            get_result -> next = new_node;
            *number_of_value = *number_of_value + 1;
            
        }
    }
    return hash_int_set;
}
void value_print(hash_node *hash_int_set, int x) {
    int value = hash_function(x);
    hash_node *temp = &hash_int_set[value];
    while (temp) {
        printf("%d\n", temp -> value);
        temp = temp -> next;
    }
}
/*      end       */

/* change hash to int array */
int *hash2arry(hash_node *hash_int_set, int number_of_value) {
    int *result = malloc(number_of_value * sizeof(int));
    int i;
    int total_number = 0;
    for (i = 0; i < 1024; i++) {
        if (hash_int_set[i].exist) {
            result[total_number] = hash_int_set[i].value;
            total_number++;
            hash_node *temp = hash_int_set[i].next;
            while (temp) {
                result[total_number] = temp -> value;
                hash_node *last = temp;
                temp = temp -> next;
                free(last);
            }
        }
    }
    free(hash_int_set);
    return result;
}