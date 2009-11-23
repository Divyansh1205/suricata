/** Copyright (c) 2009 Open Information Security Foundation.
 **  \author Pablo Rincon <pablo.rincon.crespo@gmail.com>
 ** Flowvar management for integer types, part of the detection engine
 ** Keyword: flowint
 **/

#include "eidps-common.h"
#include "decode.h"
#include "detect.h"
#include "threads.h"
#include "flow.h"
#include "flow-var.h"
#include "detect-flowint.h"
#include "util-binsearch.h"
#include "util-var-name.h"
#include "util-debug.h"
#include "util-unittest.h"

#include "detect-parse.h"
#include "detect-engine.h"
#include "detect-engine-mpm.h"


#include <inttypes.h> /* for UINT32_MAX */

/*                         name             modifiers          value      */
#define PARSE_REGEX "^\\s*([a-zA-Z][\\w\\d_]+),\\s*([+=-]{1}|==|!=|<|<=|>|>=|isset)\\s*,?\\s*([a-zA-Z][\\w\\d]+|[\\d]{1,10})?\\s*$"
/* Varnames must begin with a letter */

static pcre *parse_regex;
static pcre_extra *parse_regex_study;

int DetectFlowintMatch (ThreadVars *, DetectEngineThreadCtx *, Packet *,
                        Signature *, SigMatch *);
int DetectFlowintSetup (DetectEngineCtx *, Signature *, SigMatch *, char *);
void DetectFlowintFree (void *);
void DetectFlowintRegisterTests (void);

void DetectFlowintRegister (void)
{
    sigmatch_table[DETECT_FLOWINT].name = "flowint";
    sigmatch_table[DETECT_FLOWINT].Match = DetectFlowintMatch;
    sigmatch_table[DETECT_FLOWINT].Setup = DetectFlowintSetup;
    sigmatch_table[DETECT_FLOWINT].Free = DetectFlowintFree;
    sigmatch_table[DETECT_FLOWINT].RegisterTests = DetectFlowintRegisterTests;

    const char *eb;
    int eo;
    int opts = 0;

    parse_regex = pcre_compile (PARSE_REGEX, opts, &eb, &eo, NULL);
    if (parse_regex == NULL) {
        SCLogDebug ("pcre compile of \"%s\" failed at offset %" PRId32 ": %s",
                    PARSE_REGEX, eo, eb);
        goto error;
    }

    parse_regex_study = pcre_study (parse_regex, 0, &eb);
    if (eb != NULL) {
        SCLogDebug ("pcre study failed: %s", eb);
        goto error;
    }

    return;

error:
    return;
}

/**
 * \brief This function is used to create a flowint, add/substract values,
 *        compare it with other flowints, etc
 *
 * \param t pointer to thread vars
 * \param det_ctx pointer to the pattern matcher thread
 * \param p pointer to the current packet
 * \param s  pointer to the current Signature
 * \param m pointer to the sigmatch that we will cast into DetectFlowintData
 *
 * \retval 0 no match, when a var doesn't exist
 * \retval 1 match, when a var is initialized well, add/substracted, or a true
 * condition
 */
int DetectFlowintMatch (ThreadVars *t, DetectEngineThreadCtx *det_ctx,
                        Packet *p, Signature *s, SigMatch *m)
{
    DetectFlowintData *sfd = (DetectFlowintData *) m->ctx;
    FlowVar *fv;
    FlowVar *fvt;
    uint32_t targetval;

    if (sfd->targettype == FLOWINT_TARGET_VAR) {
        sfd->target.tvar.idx = VariableNameGetIdx (det_ctx->de_ctx,
                               sfd->target.tvar.name, DETECT_FLOWINT);

        fvt = FlowVarGet (p->flow, sfd->target.tvar.idx);
        if (fvt == NULL)
            /* We don't have that variable initialized yet */
            /* so now we need to determine what to do... */
            return 0;
        //targetval = 0;
        else
            targetval = fvt->data.fv_int.value;
    } else
        targetval = sfd->target.value;

    SCLogDebug ("Our var %s is at idx: %"PRIu16"", sfd->name, sfd->idx);

    if (sfd->modifier == FLOWINT_MODIFIER_SET) {
        FlowVarAddInt (p->flow, sfd->idx, targetval);
        SCLogDebug ("Setting %s = %u", sfd->name, targetval);
        return 1;
    }

    fv = FlowVarGet (p->flow, sfd->idx);
    if (fv != NULL && fv->datatype == FLOWVAR_TYPE_INT) {

        if (sfd->modifier == FLOWINT_MODIFIER_ADD) {
            SCLogDebug ("Adding %u to %s", targetval, sfd->name);
            FlowVarAddInt (p->flow, sfd->idx, fv->data.fv_int.value +
                           targetval);
            return 1;
        }
        if (sfd->modifier == FLOWINT_MODIFIER_SUB) {
            SCLogDebug ("Substracting %u to %s", targetval, sfd->name);
            FlowVarAddInt (p->flow, sfd->idx, fv->data.fv_int.value -
                           targetval);
            return 1;
        }

        switch (sfd->modifier) {
            case FLOWINT_MODIFIER_EQ:
                SCLogDebug ("( %u EQ %u )", fv->data.fv_int.value, targetval);
                return fv->data.fv_int.value == targetval;
                break;
            case FLOWINT_MODIFIER_NE:
                SCLogDebug ("( %u NE %u )", fv->data.fv_int.value, targetval);
                return fv->data.fv_int.value != targetval;
                break;
            case FLOWINT_MODIFIER_LT:
                SCLogDebug ("( %u LT %u )", fv->data.fv_int.value, targetval);
                return fv->data.fv_int.value < targetval;
                break;
            case FLOWINT_MODIFIER_LE:
                SCLogDebug ("( %u LE %u )", fv->data.fv_int.value, targetval);
                return fv->data.fv_int.value <= targetval;
                break;
            case FLOWINT_MODIFIER_GT:
                SCLogDebug ("( %u GT %u )", fv->data.fv_int.value, targetval);
                return fv->data.fv_int.value > targetval;
                break;
            case FLOWINT_MODIFIER_GE:
                SCLogDebug ("( %u GE %u )", fv->data.fv_int.value, targetval);
                return fv->data.fv_int.value >= targetval;
                break;
            default:
                SCLogDebug ("Unknown Modifier!");
                exit (EXIT_FAILURE);
        }
    } else {
        SCLogDebug ("Var not found!");
        /* It doesn't exist because it wasn't set
        * or it is a string var, we can't compare */
        return 0;
        /* TODO: Add a keyword "isset" */
    }

    /* It shouldn't reach this */
    return 0;
}

/**
 * \brief This function is used to parse a flowint option
 *
 * \param de_ctx pointer to the engine context
 * \param rawstr pointer to the string holding the options
 *
 * \retval NULL if invalid option
 * \retval DetectFlowintData pointer with the flowint parsed
 */
DetectFlowintData *DetectFlowintParse (DetectEngineCtx *de_ctx,
                                       char *rawstr)
{
    DetectFlowintData *sfd = NULL;
    char *str = rawstr;
    char *varname = NULL;
    char *varval = NULL;
#define MAX_SUBSTRINGS 30
    int ret = 0, res = 0;
    int ov[MAX_SUBSTRINGS];
    uint8_t modifier = FLOWINT_MODIFIER_UNKNOWN;
    unsigned long long value_long = 0;
    uint8_t dubbed = 0;
    const char *str_ptr;

    ret = pcre_exec (parse_regex, parse_regex_study, rawstr, strlen (rawstr),
                     0, 0, ov, MAX_SUBSTRINGS);
    if (ret < 3 || ret > 4) {
        SCLogDebug ("ERROR: \"%s\" is not a valid setting for flowint (ret = %d).", rawstr, ret);
        return NULL;
    }

    /* Get our flowint varname */
    res = pcre_get_substring ( (char *) rawstr, ov, MAX_SUBSTRINGS, 1, &str_ptr);
    if (res < 0) {
        SCLogDebug ("DetectFlowintParse: pcre_get_substring failed");
        return NULL;
    }
    varname = (char *) str_ptr;

    res = pcre_get_substring ( (char *) rawstr, ov, MAX_SUBSTRINGS, 2,
                               &str_ptr);
    if (res < 0) {
        SCLogDebug ("DetectFlowintParse: pcre_get_substring failed");
        return NULL;
    }

    /* Get the modifier */
    if (strcmp ("=", str_ptr) == 0)
        modifier = FLOWINT_MODIFIER_SET;
    if (strcmp ("+", str_ptr) == 0)
        modifier = FLOWINT_MODIFIER_ADD;
    if (strcmp ("-", str_ptr) == 0)
        modifier = FLOWINT_MODIFIER_SUB;

    if (strcmp ("<", str_ptr) == 0)
        modifier = FLOWINT_MODIFIER_LT;
    if (strcmp ("<=", str_ptr) == 0)
        modifier = FLOWINT_MODIFIER_LE;
    if (strcmp ("!=", str_ptr) == 0)
        modifier = FLOWINT_MODIFIER_NE;
    if (strcmp ("==", str_ptr) == 0)
        modifier = FLOWINT_MODIFIER_EQ;
    if (strcmp (">=", str_ptr) == 0)
        modifier = FLOWINT_MODIFIER_GE;
    if (strcmp (">", str_ptr) == 0)
        modifier = FLOWINT_MODIFIER_GT;
    if (strcmp ("isset", str_ptr) == 0)
        modifier = FLOWINT_MODIFIER_IS;

    if (modifier == FLOWINT_MODIFIER_UNKNOWN)
        goto error;

    sfd = malloc (sizeof (DetectFlowintData));
    if (sfd == NULL) {
        SCLogDebug ("DetectFlowintSetup malloc failed");
        goto error;
    }

    /* If we need another arg, check it out (isset doesn't need another arg) */
    if (modifier != FLOWINT_MODIFIER_IS) {
        res = pcre_get_substring ( (char *) rawstr, ov, MAX_SUBSTRINGS, 3,
                                   &str_ptr);
        varval = (char *) str_ptr;
        if (res < 0 || strcmp(varval,"") == 0) {
            SCLogDebug ("DetectFlowintParse: pcre_get_substring failed");
            return NULL;
        }
        printf("varval = %s!!!\n", varval);
        /* get the target value to operate with
        * (it should be a value or another var) */
        if (varval[0] == '\"' && varval[strlen (varval)-1] == '\"') {
            str = strdup (varval + 1);
            str[strlen (varval)-2] = '\0';
            dubbed = 1;
            if (str[0] >= '0' && str[0] <= '9') { /* is digit, look at the regexp */
                sfd->targettype = FLOWINT_TARGET_VAL;
                value_long = atoll (str);
                if (value_long > UINT32_MAX) {
                    SCLogDebug ("DetectFlowintParse: Cannot load this value."
                                " Values should be between 0 and %"PRIu32, UINT32_MAX);
                    goto error;
                }
            } else {
                sfd->targettype = FLOWINT_TARGET_VAR;
                sfd->target.tvar.name = strdup (varval);
            }
        } else {
            if (varval[0] >= '0' && varval[0] <= '9') {
                sfd->targettype = FLOWINT_TARGET_VAL;
                value_long = atoll (varval);
                if (value_long > UINT32_MAX) {
                    SCLogDebug ("DetectFlowintParse: Cannot load this value."
                                " Values should be between 0 and %"PRIu32, UINT32_MAX);
                    goto error;
                }
            } else {
                sfd->targettype = FLOWINT_TARGET_VAR;
                sfd->target.tvar.name = strdup (varval);
            }
        }
    } else {
        sfd->targettype = FLOWINT_TARGET_SELF;
    }

    /* Set the name of the origin var to modify/compared with the target */
    sfd->name = strdup (varname);
    if (de_ctx != NULL)
        sfd->idx = VariableNameGetIdx (de_ctx, varname, DETECT_FLOWINT);
    sfd->target.value = (uint32_t) value_long;

    sfd->modifier = modifier;

    if (dubbed == 1) free (str);

    return sfd;
error:
    if (dubbed == 1) free (str);
    if (sfd != NULL) free (sfd);
    return NULL;
}

/**
 * \brief This function is used to set up the SigMatch holding the flowint opt
 *
 * \param de_ctx pointer to the engine context
 * \param s  pointer to the current Signature
 * \param m pointer to the sigmatch that we will cast into DetectFlowintData
 * \param rawstr pointer to the string holding the options
 *
 * \retval 0 if all is ok
 * \retval -1 if we find any problem
 */
int DetectFlowintSetup (DetectEngineCtx *de_ctx,
                        Signature *s, SigMatch *m, char *rawstr)
{
    DetectFlowintData *sfd = NULL;
    SigMatch *sm = NULL;

    sfd = DetectFlowintParse (de_ctx, rawstr);
    if (sfd == NULL)
        goto error;

    sfd->idx = VariableNameGetIdx (de_ctx, sfd->name, DETECT_FLOWINT);

    /* Okay so far so good, lets get this into a SigMatch
     * and put it in the Signature. */
    sm = SigMatchAlloc();
    if (sm == NULL)
        goto error;

    sm->type = DETECT_FLOWINT;
    sm->ctx = (void *) sfd;

    SigMatchAppend (s, m, sm);

    return 0;

error:
    if (sfd) DetectFlowintFree (sfd);
    if (sm) free (sm);
    return -1;
}

/**
 * \brief This function is used to free the data of DetectFlowintData
 */
void DetectFlowintFree (void *tmp)
{
    DetectFlowintData *sfd = (DetectFlowintData*) tmp;
    if (sfd != NULL) {
        if (sfd->name != NULL)
            free (sfd->name);
        if (sfd->targettype == FLOWINT_TARGET_VAR)
            if (sfd->target.tvar.name != NULL)
                free (sfd->target.tvar.name);
        free (sfd);
    }
}

/**
 * \brief This is a helper funtion used for debugging purposes
 */
void DetectFlowintPrintData (DetectFlowintData *sfd)
{
    if (sfd == NULL) {
        SCLogDebug ("DetectFlowintPrintData: Error, DetectFlowintData == NULL!");
        return;
    }

    SCLogDebug ("Varname: %s, modifier: %"PRIu8", idx: %"PRIu16" Target: ",
                sfd->name, sfd->modifier, sfd->idx);
    switch (sfd->targettype) {
        case FLOWINT_TARGET_VAR:
            SCLogDebug ("target_var: %s, target_idx: %"PRIu16,
                        sfd->target.tvar.name, sfd->target.tvar.idx);
            break;
        case FLOWINT_TARGET_VAL:
            SCLogDebug ("Value: %"PRIu32"; ", sfd->target.value);
            break;
        default :
            SCLogDebug ("DetectFlowintPrintData: Error, Targettype not known!");
    }
}

#ifdef UNITTESTS
/**
 * \test DetectFlowintTestParseVal01 is a test to make sure that we set the
 *  DetectFlowint correctly for setting a valid target value
 */
int DetectFlowintTestParseVal01 (void)
{
    int result = 0;
    DetectFlowintData *sfd = NULL;
    DetectEngineCtx *de_ctx;
    de_ctx = DetectEngineCtxInit();
    if (de_ctx == NULL)
        goto error;
    de_ctx->flags |= DE_QUIET;

    sfd = DetectFlowintParse (de_ctx, "myvar,=,35");
    DetectFlowintPrintData (sfd);
    if (sfd != NULL && sfd->target.value == 35 && !strcmp (sfd->name, "myvar")
            && sfd->modifier == FLOWINT_MODIFIER_SET) {
        result = 1;
    }
    if (sfd) DetectFlowintFree (sfd);

    DetectEngineCtxFree (de_ctx);

    return result;
error:
    if (de_ctx)
        DetectEngineCtxFree (de_ctx);
    return result;
}

/**
 * \test DetectFlowintTestParseVar01 is a test to make sure that we set the
 *  DetectFlowint correctly for setting a valid target variable
 */
int DetectFlowintTestParseVar01 (void)
{
    int result = 0;
    DetectFlowintData *sfd = NULL;
    DetectEngineCtx *de_ctx;
    de_ctx = DetectEngineCtxInit();
    if (de_ctx == NULL)
        goto error;
    de_ctx->flags |= DE_QUIET;

    sfd = DetectFlowintParse (de_ctx, "myvar,=,targetvar");
    DetectFlowintPrintData (sfd);
    if (sfd != NULL && !strcmp (sfd->name, "myvar")
            && sfd->targettype == FLOWINT_TARGET_VAR
            && sfd->target.tvar.name != NULL
            && !strcmp (sfd->target.tvar.name, "targetvar")
            && sfd->modifier == FLOWINT_MODIFIER_SET) {

        result = 1;
    }
    if (sfd) DetectFlowintFree (sfd);
    DetectEngineCtxFree (de_ctx);

    return result;
error:
    if (de_ctx)
        DetectEngineCtxFree (de_ctx);
    return result;
}

/**
 * \test DetectFlowintTestParseVal02 is a test to make sure that we set the
 *  DetectFlowint correctly for adding a valid target value
 */
int DetectFlowintTestParseVal02 (void)
{
    int result = 0;
    DetectFlowintData *sfd = NULL;
    DetectEngineCtx *de_ctx;
    de_ctx = DetectEngineCtxInit();
    if (de_ctx == NULL)
        goto error;
    de_ctx->flags |= DE_QUIET;

    sfd = DetectFlowintParse (de_ctx, "myvar,+,35");
    DetectFlowintPrintData (sfd);
    if (sfd != NULL && sfd->target.value == 35 && !strcmp (sfd->name, "myvar")
            && sfd->modifier == FLOWINT_MODIFIER_ADD) {
        result = 1;
    }
    if (sfd) DetectFlowintFree (sfd);

    DetectEngineCtxFree (de_ctx);

    return result;
error:
    if (de_ctx)
        DetectEngineCtxFree (de_ctx);
    return result;
}

/**
 * \test DetectFlowintTestParseVar02 is a test to make sure that we set the
 *  DetectFlowint correctly for adding a valid target variable
 */
int DetectFlowintTestParseVar02 (void)
{
    int result = 0;
    DetectFlowintData *sfd = NULL;
    DetectEngineCtx *de_ctx;
    de_ctx = DetectEngineCtxInit();
    if (de_ctx == NULL)
        goto error;
    de_ctx->flags |= DE_QUIET;

    sfd = DetectFlowintParse (de_ctx, "myvar,+,targetvar");
    DetectFlowintPrintData (sfd);
    if (sfd != NULL && !strcmp (sfd->name, "myvar")
            && sfd->targettype == FLOWINT_TARGET_VAR
            && sfd->target.tvar.name != NULL
            && !strcmp (sfd->target.tvar.name, "targetvar")
            && sfd->modifier == FLOWINT_MODIFIER_ADD) {

        result = 1;
    }
    if (sfd) DetectFlowintFree (sfd);
    DetectEngineCtxFree (de_ctx);

    return result;
error:
    if (de_ctx)
        DetectEngineCtxFree (de_ctx);
    return result;
}

/**
 * \test DetectFlowintTestParseVal03 is a test to make sure that we set the
 *  DetectFlowint correctly for substract a valid target value
 */
int DetectFlowintTestParseVal03 (void)
{
    int result = 0;
    DetectFlowintData *sfd = NULL;
    DetectEngineCtx *de_ctx;
    de_ctx = DetectEngineCtxInit();
    if (de_ctx == NULL)
        goto error;
    de_ctx->flags |= DE_QUIET;

    sfd = DetectFlowintParse (de_ctx, "myvar,-,35");
    DetectFlowintPrintData (sfd);
    if (sfd != NULL && sfd->target.value == 35 && !strcmp (sfd->name, "myvar")
            && sfd->modifier == FLOWINT_MODIFIER_SUB) {
        result = 1;
    }
    if (sfd) DetectFlowintFree (sfd);

    DetectEngineCtxFree (de_ctx);

    return result;
error:
    if (de_ctx)
        DetectEngineCtxFree (de_ctx);
    return result;
}

/**
 * \test DetectFlowintTestParseVar03 is a test to make sure that we set the
 *  DetectFlowint correctly for substract a valid target variable
 */
int DetectFlowintTestParseVar03 (void)
{
    int result = 0;
    DetectFlowintData *sfd = NULL;
    DetectEngineCtx *de_ctx;
    de_ctx = DetectEngineCtxInit();
    if (de_ctx == NULL)
        goto error;
    de_ctx->flags |= DE_QUIET;

    sfd = DetectFlowintParse (de_ctx, "myvar,-,targetvar");
    DetectFlowintPrintData (sfd);
    if (sfd != NULL && !strcmp (sfd->name, "myvar")
            && sfd->targettype == FLOWINT_TARGET_VAR
            && sfd->target.tvar.name != NULL
            && !strcmp (sfd->target.tvar.name, "targetvar")
            && sfd->modifier == FLOWINT_MODIFIER_SUB) {

        result = 1;
    }
    if (sfd) DetectFlowintFree (sfd);
    DetectEngineCtxFree (de_ctx);

    return result;
error:
    if (de_ctx)
        DetectEngineCtxFree (de_ctx);
    return result;
}


/**
 * \test DetectFlowintTestParseVal04 is a test to make sure that we set the
 *  DetectFlowint correctly for checking if equal to a valid target value
 */
int DetectFlowintTestParseVal04 (void)
{
    int result = 0;
    DetectFlowintData *sfd = NULL;
    DetectEngineCtx *de_ctx;
    de_ctx = DetectEngineCtxInit();
    if (de_ctx == NULL)
        goto error;
    de_ctx->flags |= DE_QUIET;

    sfd = DetectFlowintParse (de_ctx, "myvar,==,35");
    DetectFlowintPrintData (sfd);
    if (sfd != NULL && sfd->target.value == 35 && !strcmp (sfd->name, "myvar")
            && sfd->modifier == FLOWINT_MODIFIER_EQ) {
        result = 1;
    }
    if (sfd) DetectFlowintFree (sfd);

    DetectEngineCtxFree (de_ctx);

    return result;
error:
    if (de_ctx)
        DetectEngineCtxFree (de_ctx);
    return result;
}

/**
 * \test DetectFlowintTestParseVar04 is a test to make sure that we set the
 *  DetectFlowint correctly for checking if equal to a valid target variable
 */
int DetectFlowintTestParseVar04 (void)
{
    int result = 0;
    DetectFlowintData *sfd = NULL;
    DetectEngineCtx *de_ctx;
    de_ctx = DetectEngineCtxInit();
    if (de_ctx == NULL)
        goto error;
    de_ctx->flags |= DE_QUIET;

    sfd = DetectFlowintParse (de_ctx, "myvar,==,targetvar");
    DetectFlowintPrintData (sfd);
    if (sfd != NULL && !strcmp (sfd->name, "myvar")
            && sfd->targettype == FLOWINT_TARGET_VAR
            && sfd->target.tvar.name != NULL
            && !strcmp (sfd->target.tvar.name, "targetvar")
            && sfd->modifier == FLOWINT_MODIFIER_EQ) {

        result = 1;
    }
    if (sfd) DetectFlowintFree (sfd);
    DetectEngineCtxFree (de_ctx);

    return result;
error:
    if (de_ctx)
        DetectEngineCtxFree (de_ctx);
    return result;
}

/**
 * \test DetectFlowintTestParseVal05 is a test to make sure that we set the
 *  DetectFlowint correctly for cheking if not equal to a valid target value
 */
int DetectFlowintTestParseVal05 (void)
{
    int result = 0;
    DetectFlowintData *sfd = NULL;
    DetectEngineCtx *de_ctx;
    de_ctx = DetectEngineCtxInit();
    if (de_ctx == NULL)
        goto error;
    de_ctx->flags |= DE_QUIET;

    sfd = DetectFlowintParse (de_ctx, "myvar,!=,35");
    DetectFlowintPrintData (sfd);
    if (sfd != NULL && sfd->target.value == 35 && !strcmp (sfd->name, "myvar")
            && sfd->modifier == FLOWINT_MODIFIER_NE) {
        result = 1;
    }
    if (sfd) DetectFlowintFree (sfd);

    DetectEngineCtxFree (de_ctx);

    return result;
error:
    if (de_ctx)
        DetectEngineCtxFree (de_ctx);
    return result;
}

/**
 * \test DetectFlowintTestParseVar05 is a test to make sure that we set the
 *  DetectFlowint correctly for checking if not equal to a valid target variable
 */
int DetectFlowintTestParseVar05 (void)
{
    int result = 0;
    DetectFlowintData *sfd = NULL;
    DetectEngineCtx *de_ctx;
    de_ctx = DetectEngineCtxInit();
    if (de_ctx == NULL)
        goto error;
    de_ctx->flags |= DE_QUIET;

    sfd = DetectFlowintParse (de_ctx, "myvar,!=,targetvar");
    DetectFlowintPrintData (sfd);
    if (sfd != NULL && !strcmp (sfd->name, "myvar")
            && sfd->targettype == FLOWINT_TARGET_VAR
            && sfd->target.tvar.name != NULL
            && !strcmp (sfd->target.tvar.name, "targetvar")
            && sfd->modifier == FLOWINT_MODIFIER_NE) {

        result = 1;
    }
    if (sfd) DetectFlowintFree (sfd);
    DetectEngineCtxFree (de_ctx);

    return result;
error:
    if (de_ctx)
        DetectEngineCtxFree (de_ctx);
    return result;
}

/**
 * \test DetectFlowintTestParseVal06 is a test to make sure that we set the
 *  DetectFlowint correctly for cheking if greater than a valid target value
 */
int DetectFlowintTestParseVal06 (void)
{
    int result = 0;
    DetectFlowintData *sfd = NULL;
    DetectEngineCtx *de_ctx;
    de_ctx = DetectEngineCtxInit();
    if (de_ctx == NULL)
        goto error;
    de_ctx->flags |= DE_QUIET;

    sfd = DetectFlowintParse (de_ctx, "myvar, >,35");
    DetectFlowintPrintData (sfd);
    if (sfd != NULL && sfd->target.value == 35 && !strcmp (sfd->name, "myvar")
            && sfd->modifier == FLOWINT_MODIFIER_GT) {
        result = 1;
    }
    if (sfd) DetectFlowintFree (sfd);

    DetectEngineCtxFree (de_ctx);

    return result;
error:
    if (de_ctx)
        DetectEngineCtxFree (de_ctx);
    return result;
}

/**
 * \test DetectFlowintTestParseVar06 is a test to make sure that we set the
 *  DetectFlowint correctly for checking if greater than a valid target variable
 */
int DetectFlowintTestParseVar06 (void)
{
    int result = 0;
    DetectFlowintData *sfd = NULL;
    DetectEngineCtx *de_ctx;
    de_ctx = DetectEngineCtxInit();
    if (de_ctx == NULL)
        goto error;
    de_ctx->flags |= DE_QUIET;

    sfd = DetectFlowintParse (de_ctx, "myvar, >,targetvar");
    DetectFlowintPrintData (sfd);
    if (sfd != NULL && !strcmp (sfd->name, "myvar")
            && sfd->targettype == FLOWINT_TARGET_VAR
            && sfd->target.tvar.name != NULL
            && !strcmp (sfd->target.tvar.name, "targetvar")
            && sfd->modifier == FLOWINT_MODIFIER_GT) {

        result = 1;
    }
    if (sfd) DetectFlowintFree (sfd);
    DetectEngineCtxFree (de_ctx);

    return result;
error:
    if (de_ctx)
        DetectEngineCtxFree (de_ctx);
    return result;
}

/**
 * \test DetectFlowintTestParseVal07 is a test to make sure that we set the
 *  DetectFlowint correctly for cheking if greater or equal than a valid target value
 */
int DetectFlowintTestParseVal07 (void)
{
    int result = 0;
    DetectFlowintData *sfd = NULL;
    DetectEngineCtx *de_ctx;
    de_ctx = DetectEngineCtxInit();
    if (de_ctx == NULL)
        goto error;
    de_ctx->flags |= DE_QUIET;

    sfd = DetectFlowintParse (de_ctx, "myvar, >= ,35");
    DetectFlowintPrintData (sfd);
    if (sfd != NULL && sfd->target.value == 35 && !strcmp (sfd->name, "myvar")
            && sfd->modifier == FLOWINT_MODIFIER_GE) {
        result = 1;
    }
    if (sfd) DetectFlowintFree (sfd);

    DetectEngineCtxFree (de_ctx);

    return result;
error:
    if (de_ctx)
        DetectEngineCtxFree (de_ctx);
    return result;
}

/**
 * \test DetectFlowintTestParseVar07 is a test to make sure that we set the
 *  DetectFlowint correctly for checking if greater or equal than a valid target variable
 */
int DetectFlowintTestParseVar07 (void)
{
    int result = 0;
    DetectFlowintData *sfd = NULL;
    DetectEngineCtx *de_ctx;
    de_ctx = DetectEngineCtxInit();
    if (de_ctx == NULL)
        goto error;
    de_ctx->flags |= DE_QUIET;

    sfd = DetectFlowintParse (de_ctx, "myvar, >= ,targetvar");
    DetectFlowintPrintData (sfd);
    if (sfd != NULL && !strcmp (sfd->name, "myvar")
            && sfd->targettype == FLOWINT_TARGET_VAR
            && sfd->target.tvar.name != NULL
            && !strcmp (sfd->target.tvar.name, "targetvar")
            && sfd->modifier == FLOWINT_MODIFIER_GE) {

        result = 1;
    }
    if (sfd) DetectFlowintFree (sfd);
    DetectEngineCtxFree (de_ctx);

    return result;
error:
    if (de_ctx)
        DetectEngineCtxFree (de_ctx);
    return result;
}

/**
 * \test DetectFlowintTestParseVal08 is a test to make sure that we set the
 *  DetectFlowint correctly for cheking if lower or equal than a valid target value
 */
int DetectFlowintTestParseVal08 (void)
{
    int result = 0;
    DetectFlowintData *sfd = NULL;
    DetectEngineCtx *de_ctx;
    de_ctx = DetectEngineCtxInit();
    if (de_ctx == NULL)
        goto error;
    de_ctx->flags |= DE_QUIET;

    sfd = DetectFlowintParse (de_ctx, "myvar, <= ,35");
    DetectFlowintPrintData (sfd);
    if (sfd != NULL && sfd->target.value == 35 && !strcmp (sfd->name, "myvar")
            && sfd->modifier == FLOWINT_MODIFIER_LE) {
        result = 1;
    }
    if (sfd) DetectFlowintFree (sfd);

    DetectEngineCtxFree (de_ctx);

    return result;
error:
    if (de_ctx)
        DetectEngineCtxFree (de_ctx);
    return result;
}

/**
 * \test DetectFlowintTestParseVar08 is a test to make sure that we set the
 *  DetectFlowint correctly for checking if lower or equal than a valid target variable
 */
int DetectFlowintTestParseVar08 (void)
{
    int result = 0;
    DetectFlowintData *sfd = NULL;
    DetectEngineCtx *de_ctx;
    de_ctx = DetectEngineCtxInit();
    if (de_ctx == NULL)
        goto error;
    de_ctx->flags |= DE_QUIET;

    sfd = DetectFlowintParse (de_ctx, "myvar, <= ,targetvar");
    DetectFlowintPrintData (sfd);
    if (sfd != NULL && !strcmp (sfd->name, "myvar")
            && sfd->targettype == FLOWINT_TARGET_VAR
            && sfd->target.tvar.name != NULL
            && !strcmp (sfd->target.tvar.name, "targetvar")
            && sfd->modifier == FLOWINT_MODIFIER_LE) {

        result = 1;
    }
    if (sfd) DetectFlowintFree (sfd);
    DetectEngineCtxFree (de_ctx);

    return result;
error:
    if (de_ctx)
        DetectEngineCtxFree (de_ctx);
    return result;
}

/**
 * \test DetectFlowintTestParseVal09 is a test to make sure that we set the
 *  DetectFlowint correctly for cheking if lower than a valid target value
 */
int DetectFlowintTestParseVal09 (void)
{
    int result = 0;
    DetectFlowintData *sfd = NULL;
    DetectEngineCtx *de_ctx;
    de_ctx = DetectEngineCtxInit();
    if (de_ctx == NULL)
        goto error;
    de_ctx->flags |= DE_QUIET;

    sfd = DetectFlowintParse (de_ctx, "myvar, < ,35");
    DetectFlowintPrintData (sfd);
    if (sfd != NULL && sfd->target.value == 35 && !strcmp (sfd->name, "myvar")
            && sfd->modifier == FLOWINT_MODIFIER_LT) {
        result = 1;
    }
    if (sfd) DetectFlowintFree (sfd);

    DetectEngineCtxFree (de_ctx);

    return result;
error:
    if (de_ctx)
        DetectEngineCtxFree (de_ctx);
    return result;
}

/**
 * \test DetectFlowintTestParseVar09 is a test to make sure that we set the
 *  DetectFlowint correctly for checking if lower than a valid target variable
 */
int DetectFlowintTestParseVar09 (void)
{
    int result = 0;
    DetectFlowintData *sfd = NULL;
    DetectEngineCtx *de_ctx;
    de_ctx = DetectEngineCtxInit();
    if (de_ctx == NULL)
        goto error;
    de_ctx->flags |= DE_QUIET;

    sfd = DetectFlowintParse (de_ctx, "myvar, < ,targetvar");
    DetectFlowintPrintData (sfd);
    if (sfd != NULL && !strcmp (sfd->name, "myvar")
            && sfd->targettype == FLOWINT_TARGET_VAR
            && sfd->target.tvar.name != NULL
            && !strcmp (sfd->target.tvar.name, "targetvar")
            && sfd->modifier == FLOWINT_MODIFIER_LT) {

        result = 1;
    }
    if (sfd) DetectFlowintFree (sfd);
    DetectEngineCtxFree (de_ctx);

    return result;
error:
    if (de_ctx)
        DetectEngineCtxFree (de_ctx);
    return result;
}

/**
 * \test DetectFlowintTestParseVar09 is a test to make sure that handle the
 * isset keyword correctly
 */
int DetectFlowintTestParseIsset10 (void)
{
    int result = 0;
    DetectFlowintData *sfd = NULL;
    DetectEngineCtx *de_ctx;
    de_ctx = DetectEngineCtxInit();
    if (de_ctx == NULL)
        goto error;
    de_ctx->flags |= DE_QUIET;

    sfd = DetectFlowintParse (de_ctx, "myvar, isset");
    DetectFlowintPrintData (sfd);
    if (sfd != NULL && !strcmp (sfd->name, "myvar")
            && sfd->targettype == FLOWINT_TARGET_SELF
            && sfd->modifier == FLOWINT_MODIFIER_IS) {

        result &= 1;
    } else {
        result = 0;
    }

    if (sfd) DetectFlowintFree (sfd);
    DetectEngineCtxFree (de_ctx);

    return result;
error:
    if (de_ctx)
        DetectEngineCtxFree (de_ctx);
    return result;
}

/**
 * \test DetectFlowintTestParseInvalidSyntaxis01 is a test to make sure that we dont set the
 *  DetectFlowint for a invalid input option
 */
int DetectFlowintTestParseInvalidSyntaxis01 (void)
{
    int result = 1;
    DetectFlowintData *sfd = NULL;
    DetectEngineCtx *de_ctx;
    de_ctx = DetectEngineCtxInit();
    if (de_ctx == NULL)
        goto error;
    de_ctx->flags |= DE_QUIET;

    sfd = DetectFlowintParse (de_ctx, "myvar,=,9999999999");
    if (sfd != NULL) {
        SCLogDebug ("DetectFlowintTestParseInvalidSyntaxis01: ERROR: invalid option at myvar,=,9532458716234857");
        result = 0;
    }
    if (sfd) DetectFlowintFree (sfd);

    sfd = DetectFlowintParse (de_ctx, "myvar,=,45targetvar");
    if (sfd != NULL) {
        SCLogDebug ("DetectFlowintTestParseInvalidSyntaxis01: ERROR: invalid option at myvar,=,45targetvar ");
        result = 0;
    }
    if (sfd) DetectFlowintFree (sfd);

    sfd = DetectFlowintParse (de_ctx, "657myvar,=,targetvar");
    if (sfd != NULL) {
        SCLogDebug ("DetectFlowintTestParseInvalidSyntaxis01: ERROR: invalid option at 657myvar,=,targetvar ");
        result = 0;
    }
    if (sfd) DetectFlowintFree (sfd);

    sfd = DetectFlowintParse (de_ctx, "myvar,=<,targetvar");
    if (sfd != NULL) {
        SCLogDebug ("DetectFlowintTestParseInvalidSyntaxis01: ERROR: invalid option at myvar,=<,targetvar ");
        result = 0;
    }
    if (sfd) DetectFlowintFree (sfd);

    sfd = DetectFlowintParse (de_ctx, "myvar,===,targetvar");
    if (sfd != NULL) {
        SCLogDebug ("DetectFlowintTestParseInvalidSyntaxis01: ERROR: invalid option at myvar,===,targetvar ");
        result = 0;
    }
    if (sfd) DetectFlowintFree (sfd);

    sfd = DetectFlowintParse (de_ctx, "myvar,==");
    if (sfd != NULL) {
        SCLogDebug ("DetectFlowintTestParseInvalidSyntaxis01: ERROR: invalid option at myvar,==");
        result = 0;
    }
    if (sfd) DetectFlowintFree (sfd);

    sfd = DetectFlowintParse (de_ctx, "myvar,");
    if (sfd != NULL) {
        SCLogDebug ("DetectFlowintTestParseInvalidSyntaxis01: ERROR: invalid option at myvar,");
        result = 0;
    }
    if (sfd) DetectFlowintFree (sfd);

    sfd = DetectFlowintParse (de_ctx, "myvar");
    if (sfd != NULL) {
        SCLogDebug ("DetectFlowintTestParseInvalidSyntaxis01: ERROR: invalid option at myvar");
        result = 0;
    }
    if (sfd) DetectFlowintFree (sfd);

    DetectEngineCtxFree (de_ctx);

    return result;
error:
    if (de_ctx)
        DetectEngineCtxFree (de_ctx);
    return result;
}


/** \test DetectFlowintTestPacket01Real
 * \brief Set a counter when we see a content:"GET"
 *        and increment it by 2 if we match a "Unauthorized"
 *        When it reach 3 (with the last +2), another counter starts
 *        and when that counter reach 6 packets.
 *
 *        All the Signatures generate an alert (its for testing)
 *        but the ignature that increment the second counter +1, that has
 *        a "noalert", so we can do all increments
 *        silently until we reach 6 next packets counted
 */
int DetectFlowintTestPacket01Real()
{
    int result = 1;

    uint8_t pkt1[] = {
        0x00, 0x1a, 0x2b, 0x19, 0x52, 0xa8, 0x00, 0x13,
        0x20, 0x65, 0x1a, 0x9e, 0x08, 0x00, 0x45, 0x00,
        0x00, 0x3c, 0xc2, 0x26, 0x40, 0x00, 0x40, 0x06,
        0xf4, 0x67, 0xc0, 0xa8, 0x01, 0xdc, 0xc0, 0xa8,
        0x01, 0x01, 0xe7, 0xf5, 0x00, 0x50, 0x17, 0x51,
        0x82, 0xb5, 0x00, 0x00, 0x00, 0x00, 0xa0, 0x02,
        0x16, 0xd0, 0xe8, 0xb0, 0x00, 0x00, 0x02, 0x04,
        0x05, 0xb4, 0x04, 0x02, 0x08, 0x0a, 0x01, 0x72,
        0x40, 0x93, 0x00, 0x00, 0x00, 0x00, 0x01, 0x03,
        0x03, 0x07
    };

    uint8_t pkt2[] = {
        0x00, 0x13, 0x20, 0x65, 0x1a, 0x9e, 0x00, 0x1a,
        0x2b, 0x19, 0x52, 0xa8, 0x08, 0x00, 0x45, 0x00,
        0x00, 0x3c, 0x00, 0x00, 0x40, 0x00, 0x40, 0x06,
        0xb6, 0x8e, 0xc0, 0xa8, 0x01, 0x01, 0xc0, 0xa8,
        0x01, 0xdc, 0x00, 0x50, 0xe7, 0xf5, 0x21, 0x04,
        0x8b, 0xdd, 0x17, 0x51, 0x82, 0xb6, 0xa0, 0x12,
        0x16, 0x80, 0x17, 0x8a, 0x00, 0x00, 0x02, 0x04,
        0x05, 0xac, 0x04, 0x02, 0x08, 0x0a, 0x01, 0x29,
        0x23, 0x63, 0x01, 0x72, 0x40, 0x93, 0x01, 0x03,
        0x03, 0x07
    };

    uint8_t pkt3[] = {
        0x00, 0x1a, 0x2b, 0x19, 0x52, 0xa8, 0x00, 0x13,
        0x20, 0x65, 0x1a, 0x9e, 0x08, 0x00, 0x45, 0x00,
        0x00, 0x34, 0xc2, 0x27, 0x40, 0x00, 0x40, 0x06,
        0xf4, 0x6e, 0xc0, 0xa8, 0x01, 0xdc, 0xc0, 0xa8,
        0x01, 0x01, 0xe7, 0xf5, 0x00, 0x50, 0x17, 0x51,
        0x82, 0xb6, 0x21, 0x04, 0x8b, 0xde, 0x80, 0x10,
        0x00, 0x2e, 0x5c, 0xa0, 0x00, 0x00, 0x01, 0x01,
        0x08, 0x0a, 0x01, 0x72, 0x40, 0x93, 0x01, 0x29,
        0x23, 0x63
    };

    uint8_t pkt4[] = {
        0x00, 0x1a, 0x2b, 0x19, 0x52, 0xa8, 0x00, 0x13,
        0x20, 0x65, 0x1a, 0x9e, 0x08, 0x00, 0x45, 0x00,
        0x01, 0x12, 0xc2, 0x28, 0x40, 0x00, 0x40, 0x06,
        0xf3, 0x8f, 0xc0, 0xa8, 0x01, 0xdc, 0xc0, 0xa8,
        0x01, 0x01, 0xe7, 0xf5, 0x00, 0x50, 0x17, 0x51,
        0x82, 0xb6, 0x21, 0x04, 0x8b, 0xde, 0x80, 0x18,
        0x00, 0x2e, 0x24, 0x39, 0x00, 0x00, 0x01, 0x01,
        0x08, 0x0a, 0x01, 0x72, 0x40, 0x93, 0x01, 0x29,
        0x23, 0x63, 0x47, 0x45, 0x54, 0x20, 0x2f, 0x20,
        0x48, 0x54, 0x54, 0x50, 0x2f, 0x31, 0x2e, 0x30,
        0x0d, 0x0a, 0x48, 0x6f, 0x73, 0x74, 0x3a, 0x20,
        0x31, 0x39, 0x32, 0x2e, 0x31, 0x36, 0x38, 0x2e,
        0x31, 0x2e, 0x31, 0x0d, 0x0a, 0x41, 0x63, 0x63,
        0x65, 0x70, 0x74, 0x3a, 0x20, 0x74, 0x65, 0x78,
        0x74, 0x2f, 0x68, 0x74, 0x6d, 0x6c, 0x2c, 0x20,
        0x74, 0x65, 0x78, 0x74, 0x2f, 0x70, 0x6c, 0x61,
        0x69, 0x6e, 0x2c, 0x20, 0x74, 0x65, 0x78, 0x74,
        0x2f, 0x63, 0x73, 0x73, 0x2c, 0x20, 0x74, 0x65,
        0x78, 0x74, 0x2f, 0x73, 0x67, 0x6d, 0x6c, 0x2c,
        0x20, 0x2a, 0x2f, 0x2a, 0x3b, 0x71, 0x3d, 0x30,
        0x2e, 0x30, 0x31, 0x0d, 0x0a, 0x41, 0x63, 0x63,
        0x65, 0x70, 0x74, 0x2d, 0x45, 0x6e, 0x63, 0x6f,
        0x64, 0x69, 0x6e, 0x67, 0x3a, 0x20, 0x67, 0x7a,
        0x69, 0x70, 0x2c, 0x20, 0x62, 0x7a, 0x69, 0x70,
        0x32, 0x0d, 0x0a, 0x41, 0x63, 0x63, 0x65, 0x70,
        0x74, 0x2d, 0x4c, 0x61, 0x6e, 0x67, 0x75, 0x61,
        0x67, 0x65, 0x3a, 0x20, 0x65, 0x6e, 0x0d, 0x0a,
        0x55, 0x73, 0x65, 0x72, 0x2d, 0x41, 0x67, 0x65,
        0x6e, 0x74, 0x3a, 0x20, 0x4c, 0x79, 0x6e, 0x78,
        0x2f, 0x32, 0x2e, 0x38, 0x2e, 0x36, 0x72, 0x65,
        0x6c, 0x2e, 0x34, 0x20, 0x6c, 0x69, 0x62, 0x77,
        0x77, 0x77, 0x2d, 0x46, 0x4d, 0x2f, 0x32, 0x2e,
        0x31, 0x34, 0x20, 0x53, 0x53, 0x4c, 0x2d, 0x4d,
        0x4d, 0x2f, 0x31, 0x2e, 0x34, 0x2e, 0x31, 0x20,
        0x47, 0x4e, 0x55, 0x54, 0x4c, 0x53, 0x2f, 0x32,
        0x2e, 0x30, 0x2e, 0x34, 0x0d, 0x0a, 0x0d, 0x0a
    };

    uint8_t pkt5[] = {
        0x00, 0x13, 0x20, 0x65, 0x1a, 0x9e, 0x00, 0x1a,
        0x2b, 0x19, 0x52, 0xa8, 0x08, 0x00, 0x45, 0x00,
        0x00, 0x34, 0xa8, 0xbd, 0x40, 0x00, 0x40, 0x06,
        0x0d, 0xd9, 0xc0, 0xa8, 0x01, 0x01, 0xc0, 0xa8,
        0x01, 0xdc, 0x00, 0x50, 0xe7, 0xf5, 0x21, 0x04,
        0x8b, 0xde, 0x17, 0x51, 0x83, 0x94, 0x80, 0x10,
        0x00, 0x2d, 0x5b, 0xc3, 0x00, 0x00, 0x01, 0x01,
        0x08, 0x0a, 0x01, 0x29, 0x23, 0x63, 0x01, 0x72,
        0x40, 0x93
    };

    uint8_t pkt6[] = {
        0x00, 0x13, 0x20, 0x65, 0x1a, 0x9e, 0x00, 0x1a,
        0x2b, 0x19, 0x52, 0xa8, 0x08, 0x00, 0x45, 0x00,
        0x01, 0xe4, 0xa8, 0xbe, 0x40, 0x00, 0x40, 0x06,
        0x0c, 0x28, 0xc0, 0xa8, 0x01, 0x01, 0xc0, 0xa8,
        0x01, 0xdc, 0x00, 0x50, 0xe7, 0xf5, 0x21, 0x04,
        0x8b, 0xde, 0x17, 0x51, 0x83, 0x94, 0x80, 0x18,
        0x00, 0x2d, 0x1b, 0x84, 0x00, 0x00, 0x01, 0x01,
        0x08, 0x0a, 0x01, 0x29, 0x23, 0x6a, 0x01, 0x72,
        0x40, 0x93, 0x48, 0x54, 0x54, 0x50, 0x2f, 0x31,
        0x2e, 0x31, 0x20, 0x34, 0x30, 0x31, 0x20, 0x55,
        0x6e, 0x61, 0x75, 0x74, 0x68, 0x6f, 0x72, 0x69,
        0x7a, 0x65, 0x64, 0x0d, 0x0a, 0x53, 0x65, 0x72,
        0x76, 0x65, 0x72, 0x3a, 0x20, 0x6d, 0x69, 0x63,
        0x72, 0x6f, 0x5f, 0x68, 0x74, 0x74, 0x70, 0x64,
        0x0d, 0x0a, 0x43, 0x61, 0x63, 0x68, 0x65, 0x2d,
        0x43, 0x6f, 0x6e, 0x74, 0x72, 0x6f, 0x6c, 0x3a,
        0x20, 0x6e, 0x6f, 0x2d, 0x63, 0x61, 0x63, 0x68,
        0x65, 0x0d, 0x0a, 0x44, 0x61, 0x74, 0x65, 0x3a,
        0x20, 0x57, 0x65, 0x64, 0x2c, 0x20, 0x31, 0x34,
        0x20, 0x4f, 0x63, 0x74, 0x20, 0x32, 0x30, 0x30,
        0x39, 0x20, 0x31, 0x33, 0x3a, 0x34, 0x39, 0x3a,
        0x35, 0x33, 0x20, 0x47, 0x4d, 0x54, 0x0d, 0x0a,
        0x57, 0x57, 0x57, 0x2d, 0x41, 0x75, 0x74, 0x68,
        0x65, 0x6e, 0x74, 0x69, 0x63, 0x61, 0x74, 0x65,
        0x3a, 0x20, 0x42, 0x61, 0x73, 0x69, 0x63, 0x20,
        0x72, 0x65, 0x61, 0x6c, 0x6d, 0x3d, 0x22, 0x44,
        0x53, 0x4c, 0x20, 0x52, 0x6f, 0x75, 0x74, 0x65,
        0x72, 0x22, 0x0d, 0x0a, 0x43, 0x6f, 0x6e, 0x74,
        0x65, 0x6e, 0x74, 0x2d, 0x54, 0x79, 0x70, 0x65,
        0x3a, 0x20, 0x74, 0x65, 0x78, 0x74, 0x2f, 0x68,
        0x74, 0x6d, 0x6c, 0x0d, 0x0a, 0x43, 0x6f, 0x6e,
        0x6e, 0x65, 0x63, 0x74, 0x69, 0x6f, 0x6e, 0x3a,
        0x20, 0x63, 0x6c, 0x6f, 0x73, 0x65, 0x0d, 0x0a,
        0x0d, 0x0a, 0x3c, 0x48, 0x54, 0x4d, 0x4c, 0x3e,
        0x3c, 0x48, 0x45, 0x41, 0x44, 0x3e, 0x3c, 0x54,
        0x49, 0x54, 0x4c, 0x45, 0x3e, 0x34, 0x30, 0x31,
        0x20, 0x55, 0x6e, 0x61, 0x75, 0x74, 0x68, 0x6f,
        0x72, 0x69, 0x7a, 0x65, 0x64, 0x3c, 0x2f, 0x54,
        0x49, 0x54, 0x4c, 0x45, 0x3e, 0x3c, 0x2f, 0x48,
        0x45, 0x41, 0x44, 0x3e, 0x0a, 0x3c, 0x42, 0x4f,
        0x44, 0x59, 0x20, 0x42, 0x47, 0x43, 0x4f, 0x4c,
        0x4f, 0x52, 0x3d, 0x22, 0x23, 0x63, 0x63, 0x39,
        0x39, 0x39, 0x39, 0x22, 0x3e, 0x3c, 0x48, 0x34,
        0x3e, 0x34, 0x30, 0x31, 0x20, 0x55, 0x6e, 0x61,
        0x75, 0x74, 0x68, 0x6f, 0x72, 0x69, 0x7a, 0x65,
        0x64, 0x3c, 0x2f, 0x48, 0x34, 0x3e, 0x0a, 0x41,
        0x75, 0x74, 0x68, 0x6f, 0x72, 0x69, 0x7a, 0x61,
        0x74, 0x69, 0x6f, 0x6e, 0x20, 0x72, 0x65, 0x71,
        0x75, 0x69, 0x72, 0x65, 0x64, 0x2e, 0x0a, 0x3c,
        0x48, 0x52, 0x3e, 0x0a, 0x3c, 0x41, 0x44, 0x44,
        0x52, 0x45, 0x53, 0x53, 0x3e, 0x3c, 0x41, 0x20,
        0x48, 0x52, 0x45, 0x46, 0x3d, 0x22, 0x68, 0x74,
        0x74, 0x70, 0x3a, 0x2f, 0x2f, 0x77, 0x77, 0x77,
        0x2e, 0x61, 0x63, 0x6d, 0x65, 0x2e, 0x63, 0x6f,
        0x6d, 0x2f, 0x73, 0x6f, 0x66, 0x74, 0x77, 0x61,
        0x72, 0x65, 0x2f, 0x6d, 0x69, 0x63, 0x72, 0x6f,
        0x5f, 0x68, 0x74, 0x74, 0x70, 0x64, 0x2f, 0x22,
        0x3e, 0x6d, 0x69, 0x63, 0x72, 0x6f, 0x5f, 0x68,
        0x74, 0x74, 0x70, 0x64, 0x3c, 0x2f, 0x41, 0x3e,
        0x3c, 0x2f, 0x41, 0x44, 0x44, 0x52, 0x45, 0x53,
        0x53, 0x3e, 0x0a, 0x3c, 0x2f, 0x42, 0x4f, 0x44,
        0x59, 0x3e, 0x3c, 0x2f, 0x48, 0x54, 0x4d, 0x4c,
        0x3e, 0x0a
    };

    uint8_t pkt7[] = {
        0x00, 0x1a, 0x2b, 0x19, 0x52, 0xa8, 0x00, 0x13,
        0x20, 0x65, 0x1a, 0x9e, 0x08, 0x00, 0x45, 0x00,
        0x00, 0x34, 0xc2, 0x29, 0x40, 0x00, 0x40, 0x06,
        0xf4, 0x6c, 0xc0, 0xa8, 0x01, 0xdc, 0xc0, 0xa8,
        0x01, 0x01, 0xe7, 0xf5, 0x00, 0x50, 0x17, 0x51,
        0x83, 0x94, 0x21, 0x04, 0x8d, 0x8e, 0x80, 0x10,
        0x00, 0x36, 0x59, 0xfa, 0x00, 0x00, 0x01, 0x01,
        0x08, 0x0a, 0x01, 0x72, 0x40, 0x9c, 0x01, 0x29,
        0x23, 0x6a
    };

    uint8_t pkt8[] = {
        0x00, 0x13, 0x20, 0x65, 0x1a, 0x9e, 0x00, 0x1a,
        0x2b, 0x19, 0x52, 0xa8, 0x08, 0x00, 0x45, 0x00,
        0x00, 0x34, 0xa8, 0xbf, 0x40, 0x00, 0x40, 0x06,
        0x0d, 0xd7, 0xc0, 0xa8, 0x01, 0x01, 0xc0, 0xa8,
        0x01, 0xdc, 0x00, 0x50, 0xe7, 0xf5, 0x21, 0x04,
        0x8d, 0x8e, 0x17, 0x51, 0x83, 0x94, 0x80, 0x11,
        0x00, 0x2d, 0x5a, 0x0b, 0x00, 0x00, 0x01, 0x01,
        0x08, 0x0a, 0x01, 0x29, 0x23, 0x6a, 0x01, 0x72,
        0x40, 0x93
    };

    uint8_t pkt9[] = {
        0x00, 0x1a, 0x2b, 0x19, 0x52, 0xa8, 0x00, 0x13,
        0x20, 0x65, 0x1a, 0x9e, 0x08, 0x00, 0x45, 0x00,
        0x00, 0x34, 0xc2, 0x2a, 0x40, 0x00, 0x40, 0x06,
        0xf4, 0x6b, 0xc0, 0xa8, 0x01, 0xdc, 0xc0, 0xa8,
        0x01, 0x01, 0xe7, 0xf5, 0x00, 0x50, 0x17, 0x51,
        0x83, 0x94, 0x21, 0x04, 0x8d, 0x8f, 0x80, 0x10,
        0x00, 0x36, 0x59, 0xef, 0x00, 0x00, 0x01, 0x01,
        0x08, 0x0a, 0x01, 0x72, 0x40, 0xa6, 0x01, 0x29,
        0x23, 0x6a
    };

    uint8_t pkt10[] = {
        0x00, 0x1a, 0x2b, 0x19, 0x52, 0xa8, 0x00, 0x13,
        0x20, 0x65, 0x1a, 0x9e, 0x08, 0x00, 0x45, 0x00,
        0x00, 0x34, 0xc2, 0x2b, 0x40, 0x00, 0x40, 0x06,
        0xf4, 0x6a, 0xc0, 0xa8, 0x01, 0xdc, 0xc0, 0xa8,
        0x01, 0x01, 0xe7, 0xf5, 0x00, 0x50, 0x17, 0x51,
        0x83, 0x94, 0x21, 0x04, 0x8d, 0x8f, 0x80, 0x11,
        0x00, 0x36, 0x57, 0x0a, 0x00, 0x00, 0x01, 0x01,
        0x08, 0x0a, 0x01, 0x72, 0x43, 0x8a, 0x01, 0x29,
        0x23, 0x6a
    };

    uint8_t pkt11[] = {
        0x00, 0x13, 0x20, 0x65, 0x1a, 0x9e, 0x00, 0x1a,
        0x2b, 0x19, 0x52, 0xa8, 0x08, 0x00, 0x45, 0x00,
        0x00, 0x34, 0x10, 0xaf, 0x40, 0x00, 0x40, 0x06,
        0xa5, 0xe7, 0xc0, 0xa8, 0x01, 0x01, 0xc0, 0xa8,
        0x01, 0xdc, 0x00, 0x50, 0xe7, 0xf5, 0x21, 0x04,
        0x8d, 0x8f, 0x17, 0x51, 0x83, 0x95, 0x80, 0x10,
        0x00, 0x2d, 0x54, 0xbb, 0x00, 0x00, 0x01, 0x01,
        0x08, 0x0a, 0x01, 0x29, 0x25, 0xc2, 0x01, 0x72,
        0x43, 0x8a
    };

    uint8_t *pkts[] = {
        pkt1, pkt2, pkt3, pkt4, pkt5, pkt6, pkt7, pkt8,
        pkt9, pkt10, pkt11
    };

    uint16_t pktssizes[] =  {
        sizeof (pkt1), sizeof (pkt2), sizeof (pkt3), sizeof (pkt4), sizeof (pkt5),
        sizeof (pkt6), sizeof (pkt7), sizeof (pkt8), sizeof (pkt9), sizeof (pkt10),
        sizeof (pkt11)
    };

    Packet p;
    DecodeThreadVars dtv;

    ThreadVars th_v;
    DetectEngineThreadCtx *det_ctx = NULL;

    memset (&dtv, 0, sizeof (DecodeThreadVars));
    memset (&th_v, 0, sizeof (th_v));

    FlowInitConfig (FLOW_QUIET);

    DetectEngineCtx *de_ctx = DetectEngineCtxInit();
    if (de_ctx == NULL) {
        goto end;
    }

    de_ctx->flags |= DE_QUIET;

    /* Now that we have the array of packets for the flow, prepare the signatures */
    de_ctx->sig_list = SigInit (de_ctx, "alert tcp any any -> any any (msg:\"Setting a flowint counter\"; content:\"GET\"; flowint: myvar,=,1; flowint: maxvar,=,6;sid:101;)");

    de_ctx->sig_list->next = SigInit (de_ctx, "alert tcp any any -> any any (msg:\"Adding to flowint counter\"; content:\"Unauthorized\"; flowint: myvar,+,2; sid:102;)");

    de_ctx->sig_list->next->next = SigInit (de_ctx, "alert tcp any any -> any any (msg:\"if the flowint counter is 3 create a new counter\"; content:\"Unauthorized\"; flowint: myvar,==,3; flowint: cntpackets, =, 0; sid:103;)");

    de_ctx->sig_list->next->next->next = SigInit (de_ctx, "alert tcp any any -> any any (msg:\"and count the rest of the packets received without generating alerts!!!\"; flowint: myvar,==,3; flowint: cntpackets, +, 1; noalert;sid:104;)");

    /* comparation of myvar with maxvar */
    de_ctx->sig_list->next->next->next->next = SigInit (de_ctx, "alert tcp any any -> any any (msg:\" and fire this when it reach 6\"; flowint: cntpackets, ==, maxvar; sid:105;)");

    /* I know it's a bit ugly, */
    de_ctx->sig_list->next->next->next->next->next = NULL;

    SigGroupBuild (de_ctx);
    //PatternMatchPrepare (mpm_ctx, MPM_B2G);
    DetectEngineThreadCtxInit (&th_v, (void *) de_ctx, (void *) &det_ctx);

    /* Get the idx of the vars we are going to track */
    uint16_t idx1, idx2;
    idx1 = VariableNameGetIdx (det_ctx->de_ctx, "myvar", DETECT_FLOWINT);
    idx2 = VariableNameGetIdx (det_ctx->de_ctx, "cntpackets", DETECT_FLOWINT);

    int i;

    /* Decode the packets, and test the matches*/
    for (i = 0;i < 11;i++) {
        memset (&p, 0, sizeof (Packet));
        DecodeEthernet (&th_v, &dtv, &p, pkts[i], pktssizes[i], NULL);

        SigMatchSignatures (&th_v, de_ctx, det_ctx, &p);

        switch (i) {
            case 3:
                if (PacketAlertCheck (&p, 101) == 0) {
                    SCLogDebug ("Not declared/initialized!");
                    result = 0;
                }
                break;
            case 5:
                if (PacketAlertCheck (&p, 102) == 0) {
                    SCLogDebug ("Not incremented!");
                    result = 0;
                }

                if (PacketAlertCheck (&p, 103) == 0) {
                    SCLogDebug ("myvar is not 3 or bad cmp!!");
                    result = 0;
                }
                break;
            case 10:
                if (PacketAlertCheck (&p, 105) == 0) {
                    SCLogDebug ("Not declared/initialized/or well incremented the"
                                " second var!");
                    result = 0;
                }
                break;
        }
        SCLogDebug ("Raw Packet %d has %u alerts ", i, p.alerts.cnt);
    }

    SigGroupCleanup (de_ctx);
    SigCleanSignatures (de_ctx);

    DetectEngineThreadCtxDeinit (&th_v, (void *) det_ctx);
    //PatternMatchDestroy (mpm_ctx);
    DetectEngineCtxFree (de_ctx);
    FlowShutdown();

    return result;

end:
    if (de_ctx) {
        SigGroupCleanup (de_ctx);
        SigCleanSignatures (de_ctx);
    }
    if (det_ctx)
        DetectEngineThreadCtxDeinit (&th_v, (void *) det_ctx);
    //PatternMatchDestroy (mpm_ctx);
    if (de_ctx)
        DetectEngineCtxFree (de_ctx);

    FlowShutdown();
    return result;
}


#endif /* UNITTESTS */

/**
 * \brief this function registers unit tests for DetectFlowint
 */
void DetectFlowintRegisterTests (void)
{
#ifdef UNITTESTS /* UNITTESTS */
    UtRegisterTest ("DetectFlowintTestParseVal01",
                    DetectFlowintTestParseVal01, 1);
    UtRegisterTest ("DetectFlowintTestParseVar01",
                    DetectFlowintTestParseVar01, 1);
    UtRegisterTest ("DetectFlowintTestParseVal02",
                    DetectFlowintTestParseVal02, 1);
    UtRegisterTest ("DetectFlowintTestParseVar02",
                    DetectFlowintTestParseVar02, 1);
    UtRegisterTest ("DetectFlowintTestParseVal03",
                    DetectFlowintTestParseVal03, 1);
    UtRegisterTest ("DetectFlowintTestParseVar03",
                    DetectFlowintTestParseVar03, 1);
    UtRegisterTest ("DetectFlowintTestParseVal04",
                    DetectFlowintTestParseVal04, 1);
    UtRegisterTest ("DetectFlowintTestParseVar04",
                    DetectFlowintTestParseVar04, 1);
    UtRegisterTest ("DetectFlowintTestParseVal05",
                    DetectFlowintTestParseVal05, 1);
    UtRegisterTest ("DetectFlowintTestParseVar05",
                    DetectFlowintTestParseVar05, 1);
    UtRegisterTest ("DetectFlowintTestParseVal06",
                    DetectFlowintTestParseVal06, 1);
    UtRegisterTest ("DetectFlowintTestParseVar06",
                    DetectFlowintTestParseVar06, 1);
    UtRegisterTest ("DetectFlowintTestParseVal07",
                    DetectFlowintTestParseVal07, 1);
    UtRegisterTest ("DetectFlowintTestParseVar07",
                    DetectFlowintTestParseVar07, 1);
    UtRegisterTest ("DetectFlowintTestParseVal08",
                    DetectFlowintTestParseVal08, 1);
    UtRegisterTest ("DetectFlowintTestParseVar08",
                    DetectFlowintTestParseVar08, 1);
    UtRegisterTest ("DetectFlowintTestParseVal09",
                    DetectFlowintTestParseVal09, 1);
    UtRegisterTest ("DetectFlowintTestParseVar09",
                    DetectFlowintTestParseVar09, 1);
    UtRegisterTest ("DetectFlowintTestParseIsset10",
                    DetectFlowintTestParseIsset10, 1);
    UtRegisterTest ("DetectFlowintTestParseInvalidSyntaxis01",
                    DetectFlowintTestParseInvalidSyntaxis01, 1);
    UtRegisterTest ("DetectFlowintTestPacket01Real",
                    DetectFlowintTestPacket01Real, 1);
#endif /* UNITTESTS */
}
