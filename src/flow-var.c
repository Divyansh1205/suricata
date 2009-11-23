/* implement per flow vars */

/* TODO
 * - move away from a linked list implementation
 * - use different datatypes, such as string, int, etc.
 * - have more than one instance of the same var, and be able to match on a 
 *   specific one, or one all at a time. So if a certain capture matches
 *   multiple times, we can operate on all of them.
 */

#include "eidps-common.h"
#include "threads.h"
#include "flow-var.h"
#include "flow.h"
#include "detect.h"

/* puts a new value into a flowvar */
void FlowVarUpdateStr(FlowVar *fv, uint8_t *value, uint16_t size) {
    if (fv->data.fv_str.value) free(fv->data.fv_str.value);
    fv->data.fv_str.value = value;
    fv->data.fv_str.value_len = size;
}

/* puts a new value into a flowvar */
void FlowVarUpdateInt(FlowVar *fv, uint32_t value) {
    fv->data.fv_int.value = value;
}

/* get the flowvar with name 'name' from the flow
 *
 * name is a normal string*/
FlowVar *FlowVarGet(Flow *f, uint8_t idx) {
    GenericVar *gv = f->flowvar;

    for ( ; gv != NULL; gv = gv->next) {
        if (gv->type == DETECT_FLOWVAR && gv->idx == idx)
            return (FlowVar *)gv;
    }

    return NULL;
}

/* add a flowvar to the flow, or update it */
void FlowVarAddStr(Flow *f, uint8_t idx, uint8_t *value, uint16_t size) {
    //printf("Adding flow var \"%s\" with value(%" PRId32 ") \"%s\"\n", name, size, value);

    SCMutexLock(&f->m);

    FlowVar *fv = FlowVarGet(f, idx);
    if (fv == NULL) {
        fv = malloc(sizeof(FlowVar));
        if (fv == NULL)
            goto out;

        fv->type = DETECT_FLOWVAR;
        fv->datatype = FLOWVAR_TYPE_STR;
        fv->idx = idx;
        fv->data.fv_str.value = value;
        fv->data.fv_str.value_len = size;
        fv->next = NULL;

        GenericVarAppend(&f->flowvar, (GenericVar *)fv);
    } else {
        FlowVarUpdateStr(fv, value, size);
    }

out:
    mutex_unlock(&f->m);
}

/* add a flowvar to the flow, or update it */
void FlowVarAddInt(Flow *f, uint8_t idx, uint32_t value) {
    //printf("Adding flow var \"%s\" with value(%" PRId32 ") \"%s\"\n", name, size, value);

    mutex_lock(&f->m);

    FlowVar *fv = FlowVarGet(f, idx);
    if (fv == NULL) {
        fv = malloc(sizeof(FlowVar));
        if (fv == NULL)
            goto out;

        fv->type = DETECT_FLOWVAR;
        fv->datatype = FLOWVAR_TYPE_INT;
        fv->idx = idx;
        fv->data.fv_int.value= value;
        fv->next = NULL;

        GenericVarAppend(&f->flowvar, (GenericVar *)fv);
    } else {
        FlowVarUpdateInt(fv, value);
    }

out:
    SCMutexUnlock(&f->m);
}

void FlowVarFree(FlowVar *fv) {
    if (fv == NULL)
        return;

    if (fv->datatype == FLOWVAR_TYPE_STR) {
            if (fv->data.fv_str.value != NULL)
                free(fv->data.fv_str.value);
    }
    free(fv);
}

void FlowVarPrint(GenericVar *gv) {
    uint16_t i;

    if (gv == NULL)
        return;

    if (gv->type == DETECT_FLOWVAR || gv->type == DETECT_FLOWINT) {
        FlowVar *fv = (FlowVar *)gv;

        if (fv->datatype == FLOWVAR_TYPE_STR) {
            printf("Name idx \"%" PRIu32 "\", Value \"", fv->idx);
            for (i = 0; i < fv->data.fv_str.value_len; i++) {
                if (isprint(fv->data.fv_str.value[i]))
                    printf("%c", fv->data.fv_str.value[i]);
                else
                    printf("\\%02X", fv->data.fv_str.value[i]);
            }
            printf("\", Len \"%" PRIu32 "\"\n", fv->data.fv_str.value_len);
        }
        if (fv->datatype == FLOWVAR_TYPE_INT) {
            printf("Name idx \"%" PRIu32 "\", Value \"%" PRIu32 "\"", fv->idx,
                    fv->data.fv_int.value);
        }
    }
    FlowVarPrint(gv->next);
}

