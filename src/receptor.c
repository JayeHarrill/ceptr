/**
 * @ingroup receptor
 *
 * @{
 * @file receptor.c
 * @brief receptor implementation
 *
 * @copyright Copyright (C) 2013-2016, The MetaCurrency Project (Eric Harris-Braun, Arthur Brock, et. al).  This file is part of the Ceptr platform and is released under the terms of the license contained in the file LICENSE (GPLv3).
 */

#include "receptor.h"
#include "stream.h"
#include "semtrex.h"
#include "process.h"
#include "accumulator.h"
#include "debug.h"
#include "mtree.h"
#include "protocol.h"
#include <stdarg.h>
#include <time.h>
#include <unistd.h>

Xaddr G_null_xaddr  = {0,0};
/*****************  create and destroy receptors */

/* set up the c structures for a receptor from a semantic tree */
Receptor * __r_init(T *t,SemTable *sem) {
    Receptor *r = malloc(sizeof(Receptor));
    r->root = t;
    r->parent = *(int *)_t_surface(_t_child(t,ReceptorInstanceParentContextIdx));
    r->context = *(int *)_t_surface(_t_child(t,ReceptorInstanceContextNumIdx));
    r->addr.addr = r->context;  //@fixme!! for now these are the same, but this needs to get fixed
    r->sem = sem;
    r->instances = NULL;
    r->q = _p_newq(r);
    r->state = Alive;  //@todo, check if this is true on unserialize

    T *state = _t_child(t,ReceptorInstanceStateIdx);
    r->flux = _t_child(state,ReceptorFluxIdx);
    r->pending_signals = _t_child(state,ReceptorPendingSignalsIdx);
    r->pending_responses = _t_child(state,ReceptorPendingResponsesIdx);
    r->conversations = _t_child(state,ReceptorConversationsIdx);
    r->edge = NULL;
    return r;
}

T *__r_add_aspect(T *flux,Aspect aspect) {
    T *a = _t_newr(flux,aspect);
    _t_newr(a,EXPECTATIONS);
    _t_newr(a,SIGNALS);
    return a;
}

T *_r_make_state() {
    T *t = _t_new_root(RECEPTOR_STATE);
    T *f = _t_newr(t,FLUX);
    __r_add_aspect(f,DEFAULT_ASPECT);
    _t_newr(t,PENDING_SIGNALS);
    _t_newr(t,PENDING_RESPONSES);
    _t_newr(t,CONVERSATIONS);
    _t_newi(t,RECEPTOR_ELAPSED_TIME,0);
    return t;
}

//helper to make empty definitions tree
T *__r_make_definitions() {
    T *defs = _t_new_root(DEFINITIONS);
    _t_newr(defs,STRUCTURES);
    _t_newr(defs,SYMBOLS);
    _t_newr(defs,PROCESSES);
    _t_newr(defs,RECEPTORS);
    _t_newr(defs,PROTOCOLS);
    _t_newr(defs,SCAPES);
    return defs;
}

/**
 * @brief Creates a new receptor
 *
 * allocates all the memory needed in the heap
 *
 * @param[in] r semantic ID for this receptor
 * @returns pointer to a newly allocated Receptor
 *
 * <b>Examples (from test suite):</b>
 * @snippet spec/receptor_spec.h testReceptorCreate
 */
Receptor *_r_new(SemTable *sem,SemanticID r) {
    T *t = _t_new_root(RECEPTOR_INSTANCE);
    _t_news(t,INSTANCE_OF,r);
    if (semeq(r,SYS_RECEPTOR)) {
        _t_newi(t,CONTEXT_NUM,0);
        _t_newi(t,PARENT_CONTEXT_NUM,-1);
    }
    else {
        _t_newi(t,CONTEXT_NUM,_d_get_receptor_context(sem,r));
        _t_newi(t,PARENT_CONTEXT_NUM,r.context);
    }
    T *state = _r_make_state();
    _t_add(t,state);
    return __r_init(t,sem);
}

/**
 * @brief Creates a new receptor from a receptor package
 *
 * allocates all the memory needed in the heap, cloning the various parts from the package
 * and binding the new receptor to the provided bindings
 *
 * @param[in] s symbol for this receptor
 * @returns pointer to a newly allocated Receptor
 * @todo implement bindings
 */
Receptor *_r_new_receptor_from_package(SemTable *sem,Symbol s,T *p,T *bindings) {
    T *defs = _t_clone(_t_child(p,3));
    //    T *aspects = _t_clone(_t_child(p,4));  @todo this should be inside the defs allready
    raise_error("fix receptor address");
    Receptor *r = _r_new(sem,s);

    //@todo fix this because it relies on SemanticTypes value matching the index order in the definitions.
    //    DO_KIDS(defs,__r_set_labels(r,_t_child(defs,i),i));

    return r;
}

T *__r_build_default_until() {
    T *until = _t_new_root(END_CONDITIONS);
    _t_newr(until,UNLIMITED);
    return until;
}

// helper to build and expectation tree
T *__r_build_expectation(Symbol carrier,T *pattern,T *action,T *with,T *until,T *using,T *cid) {
    T *e = _t_newr(0,EXPECTATION);
    _t_news(e,CARRIER,carrier);
    _t_add(e,pattern);
    _t_add(e,action);
    if (!with) with = _t_new_root(PARAMS);
    _t_add(e,with);
    if (!until) {
        until = __r_build_default_until();
    }
    _t_add(e,until);
    if (using)
        _t_add(e,using);
    if (cid) {
        _t_add(e,cid);
    }
    return e;
}

/**
 * @brief Adds an expectation to a receptor's aspect.
 *
 * @param[in] r receptor to add to
 * @param[in] aspect aspect on which to install the expectation
 * @param[in] carrier pre-screeing of signals to match against
 * @param[in] pattern semtrex to match against signals
 * @param[in] action process to run if match
 * @param[in] with parameters to pass into that process
 * @param[in] until end conditions for cleaning up this expectation
 *
 */
void _r_add_expectation(Receptor *r,Aspect aspect,Symbol carrier,T *pattern,T *action,T *with,T *until,T *using,T *cid) {
    T *e = __r_build_expectation(carrier,pattern,action,with,until,using,cid);
    __r_add_expectation(r,aspect,e);
}

void __r_add_expectation(Receptor *r,Aspect aspect,T *e) {
    T *a = __r_get_expectations(r,aspect);
    _t_add(a,e);
}

void _r_remove_expectation(Receptor *r,T *expectation) {
    T *a = _t_parent(expectation);
    _t_detach_by_ptr(a,expectation);
    _t_free(expectation);
    // @todo, if there are any processes blocked on this expectation they
    // should actually get cleaned up somehow.  This would mean searching
    // through for them, or something...
}

/**
 * Destroys a receptor freeing all the memory it uses.
 */
void _r_free(Receptor *r) {
    _t_free(r->root);
    _a_free_instances(&r->instances);
    if (r->q) _p_freeq(r->q);

    // special cases for cleaning up edge receptor resources that
    // don't get cleaned up the usual way, i.e. socket listener streams
    if (r->edge) {
        T *t;
        while(t = _t_detach_by_idx(r->edge,1)) {
            if (semeq(_t_symbol(t),EDGE_LISTENER)) {
                SocketListener *l =  (SocketListener *)_t_surface(t);
                _st_close_listener(l);
            }
            _t_free(t);
        }
        _t_free(r->edge);
    }
    free(r);
}

/*****************  receptor symbols, structures and processes */

/**
 * define a new symbol
 *
 * @param[in] r receptor to provide a structural context for symbol declarations
 * @param[in] s the structure type for this symbol
 * @param[in] label a c-string label for this symbol
 * @returns the new Symbol
 *
 */
Symbol _r_define_symbol(Receptor *r,Structure s,char *label){
    Symbol sym = _d_define_symbol(r->sem,s,label,r->context);
    return sym;
}

/**
 * define a new structure (simple version)
 *
 * this call is handy for building simple STRUCTURE_SEQUENCE style structures
 *
 * @param[in] r receptor to provide a semantic context for new structure definitions
 * @param[in] s the structure type for this symbol
 * @param[in] label a c-string label for this symbol
 * @param[in] num_params number of symbols in the structure
 * @param[in] ... variable list of Symbol type symbols
 * @returns the new Structure
 *
 */
Structure _r_define_structure(Receptor *r,char *label,int num_params,...) {
    va_list params;
    va_start(params,num_params);
    T *def = _d_make_vstruc_def(r->sem,label,num_params,params);
    va_end(params);
    Structure s = _d_define_structure(r->sem,label,def,r->context);
    return s;
}

/**
 * define a new structure
 *
 * version of _r_define_structure for complex structure defs where caller provides
 * the STRUCTURE_DEF
 *
 * @param[in] r receptor to provide a semantic context for new structure definitions
 * @param[in] s the structure type for this symbol
 * @param[in] label a c-string label for this symbol
 * @param[in] structure_def tree of STRUCTURE_DEF structure
 * @returns the new Structure
 *
 */
Structure __r_define_structure(Receptor *r,char *label,T *structure_def) {
    Structure s = _d_define_structure(r->sem,label,structure_def,r->context);
    return s;
}

/**
 * add a new process coding to a receptor
 *
 * @param[in] r the receptor
 * @param[in] code the code tree for this process
 * @param[in] name the name of the process
 * @param[in] intention a description of what the process intends to do/transform
 * @param[in] signature the signature for the process
 * @param[in] link the output signature for the process
 * @returns the new Process
 *
 */
Process _r_define_process(Receptor *r,T *code,char *name,char *intention,T *signature,T *link) {
    Process p = _d_define_process(r->sem,code,name,intention,signature,link,r->context);
    return p;
}

Protocol _r_define_protocol(Receptor *r,T *protocol_def) {
    Protocol p = _d_define_protocol(r->sem,protocol_def,r->context);
    return p;
}

/**
 * find a symbol by its label
 */
Symbol _r_get_sem_by_label(Receptor *r,char *label) {
    SemanticID sid;
    if (!__sem_get_by_label(r->sem,label,&sid,r->context))
        raise_error("label not found %s",label);
    return sid;
}

/**
 * @brief find a symbol's structure
 * @returns structure id
 */
Structure __r_get_symbol_structure(Receptor *r,Symbol s){
    return _sem_get_symbol_structure(r->sem,s);
}

/**
 * get the size of a structure's surface
 * @returns size
 */
size_t __r_get_structure_size(Receptor *r,Structure s,void *surface) {
    return _d_get_structure_size(r->sem,s,surface);
}
/**
 * get the size of a symbol's surface
 * @returns size
 */
size_t __r_get_symbol_size(Receptor *r,Symbol s,void *surface) {
    return _d_get_symbol_size(r->sem,s,surface);
}

/**
 * Walks the definition of a symbol to build a semtrex that would match that definiton
 *
 * @param[in] r the receptor context in which things are defined
 * @param[in] s the symbol to build a semtrex for
 * @returns the completed semtrex
 */
T * _r_build_def_semtrex(Receptor *r,Symbol s) {
    return _d_build_def_semtrex(r->sem,s,0);
}

/**
 * Determine whether a tree matches a symbol definition, both structural and semantic
 *
 * @param[in] r the receptor context in which things are defined
 * @param[in] s the symbol we expect this tree to be
 * @param[in] t the tree to match
 * @returns true or false depending on the match
 *
 * @todo currently this just matches on a semtrex.  It should also look at the surface
 sizes to see if they meet the criteria of the structure definitions.
 *
 * <b>Examples (from test suite):</b>
 * @snippet spec/receptor_spec.h testReceptorDefMatch
 */
int _r_def_match(Receptor *r,Symbol s,T *t) {
    T *stx = _r_build_def_semtrex(r,s);
    int result = _t_match(stx,t);
    _t_free(stx);
    return result;
}

/*****************  receptor instances and xaddrs */

/**
 * Create a new instance of a tree
 *
 * @param[in] r the receptor context in which things are defined
 * @param[in] t the tree to instantiate
 * @returns xaddr of the instance
 *
 * @todo currently stores instances in a hash of hashes, this will later
 * be handled by interacting with the data-engine.
 *
 * <b>Examples (from test suite):</b>
 * @snippet spec/receptor_spec.h testReceptorInstances
 */
Xaddr _r_new_instance(Receptor *r,T *t) {
    return _a_new_instance(&r->instances,t);
}

/**
 * retrieve the instance for a given xaddr
 *
 * @param[in] r the receptor context in which things are defined
 * @param[in] x the xaddr of the instance
 * @returns the instance tree
 *
 * <b>Examples (from test suite):</b>
 * @snippet spec/receptor_spec.h testReceptorInstances
 */
T * _r_get_instance(Receptor *r,Xaddr x) {
    return _a_get_instance(&r->instances,x);
}

/**
 * set the instance for a given xaddr
 *
 * @param[in] r the receptor context in which things are defined
 * @param[in] x the xaddr of the instance
 * @param[in] t the new tree to set the instance value to
 * @returns the instance tree
 *
 * <b>Examples (from test suite):</b>
 * @snippet spec/receptor_spec.h testReceptorInstances
 */
T * _r_set_instance(Receptor *r,Xaddr x,T *t) {
    return _a_set_instance(&r->instances,x,t);
}

/**
 * delete the instance for a given xaddr
 *
 * @param[in] r the receptor context in which things are defined
 * @param[in] x the xaddr of the instance
 *
 * <b>Examples (from test suite):</b>
 * @snippet spec/receptor_spec.h testReceptorInstances
 */
T * _r_delete_instance(Receptor *r,Xaddr x) {
    _a_delete_instance(&r->instances,x);
}

/**
 * get the hash of a tree by Xaddr
 */
TreeHash _r_hash(Receptor *r,Xaddr t) {
    return _t_hash(r->sem,_r_get_instance(r,t));
}

/******************  receptor serialization */

/**
 * Serialize a receptor
 *
 * Allocates a buffer for and serializes a receptor into the buffer
 *
 * @param[in] r Receptor to serialize
 * @param[inout] surfaceP pointer to a void * to hold the resulting serialized data
 * @param[inout] lengthP pointer to a size_t to hold the resulting serialized data length
 *
 * <b>Examples (from test suite):</b>
 * @snippet spec/receptor_spec.h testReceptorSerialize
 */
void _r_serialize(Receptor *r,void **surfaceP,size_t *lengthP) {
    /* size_t buf_size = 10000; */
    /* *surfaceP  = malloc(buf_size); */
    /* *lengthP = __t_serialize(&r->defs,r->root,surfaceP,sizeof(size_t),buf_size,0); */
    /* *(size_t *)(*surfaceP) = *lengthP; */

    H h = _m_new_from_t(r->root);
    S *s = _m_serialize(h.m);

    S *is = __a_serialize_instances(&r->instances);
    s = (S *)realloc(s,s->total_size+is->total_size);
    memcpy(((void *)s)+s->total_size,is,is->total_size);

    *lengthP = s->total_size+is->total_size;
    *surfaceP = (void *)s;
    free(is);
    _m_free(h);
}

/**
 * Unserialize a receptor
 *
 * Given a serialized receptor, return an instantiated receptor tree
 *
 * @param[in] surface serialized receptor data
 * @returns Receptor
 */
Receptor * _r_unserialize(SemTable *sem,void *surface) {

    S *s = (S *)surface;
    H h = _m_unserialize(s);

    T *t = _t_new_from_m(h);
    _m_free(h);

    Receptor *r = __r_init(t,sem);

    /* size_t length = *(size_t *)surface; */
    /* Receptor *r = _r_new(*(Symbol *)(surface+sizeof(size_t))); */
    /* surface += sizeof(size_t); */
    /* T *t =  _t_unserialize(&r->defs,&surface,&length,0); */
    /* _t_free(r->root); */
    /* r->root = t; */
    //    T *defs = _t_child(t,1);
    //  DO_KIDS(defs,__r_set_labels(r,_t_child(defs,i),i));

    // move to the instances
    s = (S *) (surface + s->total_size);
    __a_unserialize_instances(sem,&r->instances,(S *)s);
    return r;
}

/******************  receptor signaling */

// build a receptor address.  This is scaffolding for later receptor
// addressing that will include both ceptrnet addresses and receptor paths
// as a possible options for addressing the receptor.
T *___r_make_addr(T *parent,Symbol type,ReceptorAddress addr,bool is_run_node) {
    T *a = __t_newr(parent,type,is_run_node);
    __t_newi(a,RECEPTOR_ADDR,addr.addr,is_run_node);
    return a;
}

ReceptorAddress __r_get_addr(T *addr) {
    // for now they are all instance nums so we can just get the surface
    // of the first child.
    T *t = _t_child(addr,1);
    return *(ReceptorAddress *)_t_surface(t);
}

/**
 * build a signal
 *
 * @param[in] from source Receptor Xaddr
 * @param[in] to destination Receptor Xaddr
 * @param[in] aspect Aspect over which the message will be sent
 * @param[in] carrier Carrier used for matching expectations
 * @param[in] signal_contents the message to be sent, which will be wrapped in a BODY
 * @param[in] in_response_to optional IN_RESPONSE_TO_UUID for request response
 * @param[in] until optional END_CONDITIONS for a request
 * @param[in] conversation optional conversation id for signals that should be routed to a conversation
 * @todo signal should have timestamps
 */
T* __r_make_signal(ReceptorAddress from,ReceptorAddress to,Aspect aspect,Symbol carrier,T *signal_contents,UUIDt *in_response_to,T* until,T *cid) {
    T *s = _t_new_root(SIGNAL);
    T *e = _t_newr(s,ENVELOPE);
    T *m = _t_newr(s,MESSAGE);
    T *h = _t_newr(m,HEAD);
    // @todo convert to paths at some point?
    __r_make_addr(h,FROM_ADDRESS,from);
    __r_make_addr(h,TO_ADDRESS,to);
    _t_news(h,ASPECT_IDENT,aspect);
    _t_news(h,CARRIER,carrier);
    UUIDt t = __uuid_gen();
    _t_new(e,SIGNAL_UUID,&t,sizeof(UUIDt));
    T *b = _t_newt(m,BODY,signal_contents);

    if (in_response_to && until) raise_error("attempt to make signal with both response_uuid and until");
    if (in_response_to)
        _t_new(h,IN_RESPONSE_TO_UUID,in_response_to,sizeof(UUIDt));
    else if (until)
        _t_add(h,until);
    if (cid) {
        _t_add(h,_t_clone(cid));
    }
    return s;
}

// low level send, must be called with pending_signals resource locked!!
T* __r_send(Receptor *r,T *signal) {
    _t_add(r->pending_signals,signal);

    //@todo for now we return the UUID of the signal as the result.  Perhaps later we return an error condition if delivery to address is known to be impossible, or something like that.
    T *envelope = _t_child(signal,SignalEnvelopeIdx);
    return _t_rclone(_t_child(envelope,EnvelopeSignalUUIDIdx));
}

/**
 * send a simple signal (say)
 *
 * @param[in] r the receptor sending the signal
 * @param[in] signal Signal tree
 * @returns a clone of the UUID of the message sent.
 */
T* _r_send(Receptor *r,T *signal) {
    debug(D_SIGNALS,"sending %s\n",_t2s(r->sem,signal));
    //@todo lock resources
    T *result =__r_send(r,signal);
    //@todo unlock resources
    return result;
}

/**
 * send a request signal
 *
 * @param[in] r sending receptor
 * @param[in] signal Signal tree
 * @param[in] response_carrier the carrier on which to expect a response
 * @param[in] code_point the point in the code to re-awaken when a response comes back
 * @param[in] process_id the id of the process in which that code point exists
 * @returns a clone of the UUID of the message sent.
 * @todo signal should have timestamps and other meta info
 */
T* _r_request(Receptor *r,T *signal,Symbol response_carrier,T *code_point,int process_id,T *cid) {

    //@todo lock resources
    T *result = __r_send(r,signal); // result is signal UUID
    T *pr = _t_newr(r->pending_responses,PENDING_RESPONSE);
    _t_add(pr,_t_clone(result));
    _t_news(pr,CARRIER,response_carrier);
    _t_add(pr,__p_build_wakeup_info(code_point,process_id));
    int p[] = {SignalMessageIdx,MessageHeadIdx,HeadOptionalsIdx,TREE_PATH_TERMINATOR};
    T *ec = _t_get(signal,p);
    if (!ec || !semeq(_t_symbol(ec),END_CONDITIONS)) raise_error("request missing END_CONDITIONS");
    _t_add(pr,_t_clone(ec));
    if (cid) _t_add(pr,_t_clone(cid));

    debug(D_SIGNALS,"sending request and adding pending response: %s\n",_td(r,pr));
    //@todo unlock resources

    return result;
}

// check if the end condition has been met
// @todo find the correct home for this function
void evaluateEndCondition(T *ec,bool *cleanup,bool *allow) {
    *cleanup = false;
    *allow = false;
    int k = _t_children(ec);
    while (k) {
        T *c = _t_child(ec,k);
        Symbol sym = _t_symbol(c);
        if (semeq(sym,COUNT)) {
            //@todo mutex!!
            int *cP = (int *)_t_surface(c);
            if (*cP <= 1) *cleanup = true;
            if (*cP >= 1) *allow = true;
            (*cP)--;
            debug(D_SIGNALS,"decreasing count to: %d\n",*cP);
            break;  // this is final, even if there's a timeout
        }
        else if (semeq(sym,TIMEOUT_AT)) {
            T *td = _t_child(c,1);
            T *nw = _t_child(c,2);
            int year = *(int *)_t_surface(_t_child(td,1))-1900;
            int mon = *(int *)_t_surface(_t_child(td,2))-1;
            int mday = *(int *)_t_surface(_t_child(td,3));
            int hour = *(int *)_t_surface(_t_child(nw,1));
            int min = *(int *)_t_surface(_t_child(nw,2));
            int sec = *(int *)_t_surface(_t_child(nw,3));

            //debug(D_SIGNALS,"T: y:%d,m:%d,d:%d,h:%d,m:%d,s:%d\n")
            time_t now_t;
            time(&now_t);
            struct tm now;
            gmtime_r(&now_t, &now);
            if ((year > now.tm_year) ||
                (mon > now.tm_mon) ||
                (mday > now.tm_mday) ||
                (hour > now.tm_hour) ||
                (min > now.tm_min) ||
                (sec > now.tm_sec)) {
                *allow = true;
            }

            *cleanup = !*allow;
        }
        else if (semeq(sym,UNLIMITED)) {
            *allow = true;
        }
        else {
            raise_error("unknown end condition %s",t2s(c));
        }

        k--;
    }
    debug(D_SIGNALS,"after end condition %s cleanup=%s allow=%s\n",t2s(ec),*cleanup?"true":"false",*allow?"true":"false");
}

/**
 * low level function for testing expectation patterns on signals and either adding a new run tree
 * onto the current Q or reawakening the process that's been blocked waiting for the expectation
 * to match
 */
void __r_test_expectation(Receptor *r,T *expectation,T *signal) {
    Q *q = r->q;
    int p[] = {SignalMessageIdx,MessageBodyIdx,TREE_PATH_TERMINATOR};
    T *body = _t_get(signal,p);
    T *signal_contents = (T *)_t_surface(body);

    //test carriers first because they must match
    T *e_carrier = _t_child(expectation,ExpectationCarrierIdx);
    T *head =_t_getv(signal,SignalMessageIdx,MessageHeadIdx,TREE_PATH_TERMINATOR);
    T *s_carrier = _t_child(head,HeadCarrierIdx);

    debug(D_SIGNALS,"checking signal carrier %s\n",_td(q->r,s_carrier));
    debug(D_SIGNALS,"against expectation carrier %s\n",_td(q->r,e_carrier));

    Symbol esym = *(Symbol *)_t_surface(e_carrier);
    if (!semeq(esym,*(Symbol *)_t_surface(s_carrier)) && !semeq(esym,NULL_SYMBOL)) return;

    T *s_cid = __t_find(head,CONVERSATION_IDENT,HeadOptionalsIdx);
    T *e_cid = __t_find(expectation,CONVERSATION_IDENT,ExpectationOptionalsIdx);
    debug(D_SIGNALS,"checking signal conversation %s\n",_td(q->r,s_cid));
    debug(D_SIGNALS,"against expectation conversation %s\n",_td(q->r,e_cid));

    // if expectation is keyed to a conversation and the signal isn't the instant no match
    if (e_cid && !s_cid) return;
    // if both signal and expectation are keyed to a conversation test the ids for equality
    if (s_cid && e_cid) {
        if (!__cid_equal(r->sem,s_cid,e_cid)) return;
    }

    T *pattern,*m;
    pattern = _t_child(expectation,ExpectationPatternIdx);
    // if we get a match, create a run tree from the action, using the match and signal as the parameters
    T *stx = _t_news(0,SEMTREX_GROUP,NULL_SYMBOL);
    _t_add(stx,_t_clone(_t_child(pattern,1)));
    debug(D_SIGNALS,"matching %s\n",_td(q->r,signal_contents));
    debug(D_SIGNALS,"against %s\n",_td(q->r,stx));

    bool matched;
    matched = _t_matchr(stx,signal_contents,&m);
    bool allow;
    bool cleanup;
    evaluateEndCondition(_t_child(expectation,ExpectationEndCondsIdx),&cleanup,&allow);

    if (allow && matched) {
        debug(D_SIGNALS,"got a match on %s\n",_td(q->r,stx));

        T *rt=0;
        T *action = _t_child(expectation,ExpectationActionIdx);
        if (!action) {
            raise_error("null action in expectation!");
        }

        if (semeq(_t_symbol(action),WAKEUP_REFERENCE)) {
            /* // for now we add the params to the contexts run tree */
            /* /// @todo later this should be integrated into some kind of scoping handling */
            T *params = _t_rclone(_t_child(expectation,ExpectationParamsIdx));
            _p_fill_from_match(r->sem,params,m,signal_contents);
            _p_wakeup(q,action,params,noReductionErr);
            cleanup = true; //always cleanup after a wakeup because the context is gone.
        }
        else {
            Process p = *(Process*) _t_surface(action);

            // _p_make_run_tree assumes rT nodes
            T *params = _t_rclone(_t_child(expectation,ExpectationParamsIdx));
            _p_fill_from_match(r->sem,params,m,signal_contents);
            T *sm = __t_find(expectation,SEMANTIC_MAP,ExpectationOptionalsIdx);
            if (sm) sm = _t_clone(sm);
            debug(D_SIGNALS,"creating a run tree for action %s with params %s\n",_sem_get_name(r->sem,p),_t2s(r->sem,params));
            //@todo check the signature?
            rt = _p_make_run_tree(r->sem,p,params,sm);
            _t_free(params);
            _t_add(signal,rt);
            __p_addrt2q(q,rt,sm);
        }
        _t_free(m);
    }
    if (cleanup) {
        debug(D_SIGNALS,"cleaning up %s\n",_td(q->r,expectation));
        _r_remove_expectation(q->r,expectation);
    }

    _t_free(stx);
}

// what kind of sanatizing do we do of the actual response signal?
T* __r_sanatize_response(Receptor *r,T* response) {
    return _t_rclone(response);
}

int __r_deliver_response(Receptor *r,T *response_to,T *signal) {
    T *head = _t_getv(signal,SignalMessageIdx,MessageHeadIdx,TREE_PATH_TERMINATOR);
    // responses don't trigger expectation matching, instead they
    // go through the pending_responses list to see where the signal goes
    UUIDt *u = (UUIDt*)_t_surface(response_to);
    debug(D_SIGNALS,"Delivering response: %s\n",_td(r,signal));
    Symbol signal_carrier = *(Symbol *)_t_surface(_t_child(head,HeadCarrierIdx));

    T *body = _t_getv(signal,SignalMessageIdx,MessageBodyIdx,TREE_PATH_TERMINATOR);
    T *response = (T *)_t_surface(body);
    T *l;
    DO_KIDS(r->pending_responses,
            l = _t_child(r->pending_responses,i);
            if (__uuid_equal(u,(UUIDt *)_t_surface(_t_child(l,PendingResponseUUIDIdx)))) {

                // get the end conditions so we can see if we should actually respond
                T *ec = _t_child(l,PendingResponseEndCondsIdx);
                bool allow;
                bool cleanup;
                evaluateEndCondition(ec,&cleanup,&allow);

                if (allow) {
                    Symbol carrier = *(Symbol *)_t_surface(_t_child(l,PendingResponseCarrierIdx));
                    T *wakeup = _t_child(l,PendingResponseWakeupIdx);
                    // now set up the signal so when it's freed below, the body doesn't get freed too
                    signal->context.flags &= ~TFLAG_SURFACE_IS_TREE;
                    if (!semeq(carrier,signal_carrier)) {
                        debug(D_SIGNALS,"response failed carrier check, expecting %s, but got %s!\n",_r_get_symbol_name(r,carrier),_r_get_symbol_name(r,signal_carrier));
                        //@todo what kind of logging of these kinds of events?
                        break;
                    }

                    response = __r_sanatize_response(r,response);
                    // if the response isn't safe just break
                    if (!response) {
                        //@todo figure out if this means we should throw away the pending response too
                        break;
                    }
                    _p_wakeup(r->q,wakeup,response,noReductionErr);
                }

                if (cleanup) {
                    debug(D_SIGNALS,"removing pending response: %s\n",_td(r,l));
                    _t_detach_by_idx(r->pending_responses,i);
                    //i--;
                    _t_free(l);
                }
                break;
            }
            );
    _t_free(signal);
    return noDeliveryErr;
}


bool __cid_equal(SemTable *sem,T *cid1,T*cid2) {
    UUIDt *u1 = __cid_getUUID(cid1);
    UUIDt *u2 = __cid_getUUID(cid2);
    return __uuid_equal(u1,u2);
    //    return _t_hash(sem,cid1) == _t_hash(sem,cid2);
}

T *__cid_new(T *parent,UUIDt *c,T *topic) {
    T *cid = _t_newr(parent,CONVERSATION_IDENT);
    _t_new(cid,CONVERSATION_UUID,c,sizeof(UUIDt));
    return cid;
}

UUIDt *__cid_getUUID(T *cid) {
    return (UUIDt *)_t_surface(_t_child(cid,ConversationIdentUUIDIdx));
}

// registers a new conversation at the receptor level.  Note that this routine
// expects that the until param (if provided) can be added to the conversation tree,
// i.e. it must not be part of some other tree.
T * _r_add_conversation(Receptor *r,UUIDt *parent_u,UUIDt *u,T *until,T *wakeup) {
    T *c = _t_new_root(CONVERSATION);
    T *cu = __cid_new(c,u,0);

    _t_add(c, until ? until : __r_build_default_until());
    _t_newr(c,CONVERSATIONS); // add the root for any sub-conversations
    if (wakeup) _t_add(c,wakeup);

    //@todo NOT THREAD SAFE, add locking
    T *p;
    if (parent_u) {
        p = _r_find_conversation(r,parent_u);
        p = _t_child(p,ConversationConversationsIdx);
        if (!p) raise_error("parent conversation not found!");
    }
    else p = r->conversations;
    _t_add(p,c);
    //@todo UNLOCK
    return c;
}

// finds a conversation searching recursively through sub-conversations
T *__r_find_conversation(T *conversations, UUIDt *uuid) {
    T *c,*ci;
    bool found = false;

    // @todo lock?
    DO_KIDS(conversations,
            c = _t_child(conversations,i);
            UUIDt *u = __cid_getUUID(_t_child(c,ConversationIdentIdx));
            if (__uuid_equal(uuid,u)) {
                found = true;
                break;
            }
            T *sub_conversations = _t_child(c,ConversationConversationsIdx);
            if (_t_children(sub_conversations)) {
                c = __r_find_conversation(sub_conversations,uuid);
                if (c) {found = true;break;}
            }
            );
    // @todo unlock
    return found?c:NULL;
}

T *_r_find_conversation(Receptor *r, UUIDt *uuid) {
    // @todo reimplement with semtrex?
    return __r_find_conversation(r->conversations, uuid);
}


typedef void (*doConversationFn)(T *,void *);
void __r_walk_conversation(T *conversation, doConversationFn fn,void *param) {
    (*fn)(_t_child(conversation,ConversationIdentIdx),param);

    T *conversations = _t_child(conversation,ConversationConversationsIdx);
    if (_t_children(conversations)) {
        T *c;
        DO_KIDS(conversations,
                c = _t_child(conversations,i);
                __r_walk_conversation(c,param,fn);
            );
    }
}

void _cleaner(T *cid,void *p) {
    Receptor *r = (Receptor *)p;
    UUIDt *u = __cid_getUUID(cid);
    T *e,*ex;
    int i,j;
    // remove any pending listeners that were established in the covnersation
    // @todo implement saving expectations in conversations into a hash
    // so we don't have to do this ugly n^2 search...
    for(j=1;j<=_t_children(r->flux);j++) {
        ex = _t_child(_t_child(r->flux,j),aspectExpectationsIdx);
        for(i=1;i<=_t_children(ex);i++) {
            e = _t_child(ex,i);
            T *cid = __t_find(e,CONVERSATION_IDENT,ExpectationOptionalsIdx);
            if (cid && __uuid_equal(u,__cid_getUUID(cid))) {
                _t_detach_by_ptr(ex,e);
                _t_free(e);
                i--;
            }
        }
    }
    // remove any pending response handlers from requests
    for(i=1;i<=_t_children(r->pending_responses);i++) {
        e = _t_child(r->pending_responses,i);
        T *cid = _t_child(e,PendingResponseConversationIdentIdx);
        if (cid && __uuid_equal(u,__cid_getUUID(cid))) {
            _t_detach_by_ptr(r->pending_responses,e);
            _t_free(e);
            i--;
        }
    }
}

// cleans up any pending requests, listens and the conversation record
// returns the wakeup reference
T * __r_cleanup_conversation(Receptor *r, UUIDt *cuuid) {
    // @todo lock conversations?
    T *c = _r_find_conversation(r,cuuid);
    if (!c) {
        raise_error("can't find conversation");
    }
    T *w = _t_detach_by_idx(c,ConversationWakeupIdx);

    __r_walk_conversation(c,_cleaner,r);

    _t_detach_by_ptr(_t_parent(c),c);
    _t_free(c);
    // @todo unlock conversations?
    return w;
}

/**
 * Send a signal to a receptor
 *
 * This function checks to see if the signal is a response and if so activates the run-tree/action that's
 * waiting for that response with the signal contents as the response value/param
 * or, if it's a new signal, adds it to the flux, and then runs through all the
 * expectations on the aspect the signal was sent on to see if it matches any expectation, and if so, builds
 * action run-trees and adds them to receptor's process queue.
 *
 * @param[in] r destination receptor
 * @param[in] signal signal to be delivered to the receptor
 * @todo for now the signal param is added directly to the flux.  Later it should probably be cloned? or there should be a parameter to choose?
 *
 * @returns Error
 * @todo figure out what kinds of errors could be returned by _r_deliver
 *
 * <b>Examples (from test suite):</b>
 * @snippet spec/receptor_spec.h testReceptorAction
 */
Error _r_deliver(Receptor *r, T *signal) {

    T *head = _t_getv(signal,SignalMessageIdx,MessageHeadIdx,TREE_PATH_TERMINATOR);

    T *conversation = NULL;
    T *end_conditions = NULL;
    T *response_to = NULL;

    // check the optional HEAD items to see if this is more than a plain signal
    int optionals = HeadOptionalsIdx;
    T *extra;
    while(extra =_t_child(head,optionals++)) {
        Symbol sym = _t_symbol(extra);
        if (semeq(CONVERSATION_IDENT,sym))
            conversation = extra;
        else if (semeq(IN_RESPONSE_TO_UUID,sym))
            response_to = extra;
        else if (semeq(END_CONDITIONS,sym))
            end_conditions = extra;
    }

    // if there is a conversation, check to see if we've got a scope open for it
    if (conversation) {
        UUIDt *cuuid = __cid_getUUID(conversation);
        T *c = _r_find_conversation(r,cuuid);
        if (!c) {
            c = _r_add_conversation(r,0,cuuid,end_conditions,NULL);
        }
    }

    // if there is an IN_RESPONSE_TO_UUID then we know it's a response
    if (response_to) {
        return __r_deliver_response(r,response_to,signal);
    }
    else {

        // if there are END_CONDITIONS we know this is a request
        if (end_conditions) {
            // determine if we will honor the request conditions?
            // perhaps that all happens at the protocol level
            // @todo anything specific we need to store here??
        }

        Aspect aspect = *(Aspect *)_t_surface(_t_child(head,HeadAspectIdx));

        T *as = __r_get_signals(r,aspect);

        debug(D_SIGNALS,"Delivering: %s\n",_td(r,signal));
        _t_add(as,signal);
        // walk through all the expectations on the aspect and see if any expectations match this incoming signal
        T *es = __r_get_expectations(r,aspect);
        debug(D_SIGNALS,"Testing %d expectations\n",es ? _t_children(es) : 0);
        T *l;
        DO_KIDS(es,
                l = _t_child(es,i);
                __r_test_expectation(r,l,signal);
                );
    }
    return noDeliveryErr;
}

/******************  internal utilities */

T *__r_get_aspect(Receptor *r,Aspect aspect) {
    int i;
    T *a;
    DO_KIDS(r->flux,
            a = _t_child(r->flux,i);
            if (semeq(aspect,_t_symbol(a)))
                return a;
            );
    a = __r_add_aspect(r->flux,aspect);
    return a;
}
T *__r_get_expectations(Receptor *r,Aspect aspect) {
    return _t_child(__r_get_aspect(r,aspect),aspectExpectationsIdx);
}
T *__r_get_signals(Receptor *r,Aspect aspect) {
    return _t_child(__r_get_aspect(r,aspect),aspectSignalsIdx);
}


/**
 * get the Receptor structure from an installed receptor
 */
Receptor * __r_get_receptor(T *installed_receptor) {
    if (! is_receptor(_t_symbol(installed_receptor)))
        raise_error("expecting SEM_TYPE_RECEPTOR!");
    return (Receptor *)_t_surface(installed_receptor);
}

/*****************  Tree debugging utilities */

char *_r_get_symbol_name(Receptor *r,Symbol s) {
    return _sem_get_name(r->sem,s);
}

char *_r_get_structure_name(Receptor *r,Structure s) {
    return _sem_get_name(r->sem,s);
}

char *_r_get_process_name(Receptor *r,Process p) {
    return _sem_get_name(r->sem,p);
}

char __t_dump_buf[10000];

char *_td(Receptor *r,T *t) {
    __td(r,t,__t_dump_buf);
}

char *__td(Receptor *r,T *t,char *buf) {
    if (!t) sprintf(buf,"<null-tree>");
    else
        __t_dump(r->sem,t,0,buf);
    return buf;
}

/*****************  Built-in core and edge receptors */
// functions for stream edge receptors

Receptor *_r_makeStreamEdgeReceptor(SemTable *sem) {
    Receptor *r = _r_new(sem,STREAM_EDGE);
    return r;
}

void __r_listenerCallback(Stream *st,void *arg) {
    Receptor *r = (Receptor *)arg;

    T *code = _t_rclone(_t_child(r->edge,2));
    T *params = _t_clone(_t_child(r->edge,3));
    _t_new_cptr(params,EDGE_STREAM,st);
    T *err_handler = _t_child(r->edge,4);

    T *run_tree = _t_new_root(RUN_TREE);
    _t_add(run_tree,code);
    _t_add(run_tree,params);
    if (err_handler) {
        _t_add(run_tree,_t_rclone(err_handler));
    }

    _p_addrt2q(r->q,run_tree);

}

SocketListener *_r_addListener(Receptor *r,int port,T *code,T*params,T *err_handler,char *delim) {
    T *e = _t_new_root(PARAMS);

    SocketListener *l = _st_new_socket_listener(port,__r_listenerCallback,r,delim);
    _t_new_cptr(e,EDGE_LISTENER,l);
    _t_add(e,code);
    if (!params) params = _t_newr(e,PARAMS);
    else _t_add(e,params);
    if (err_handler) _t_add(e,err_handler);

    if (r->edge) raise_error("edge in use!!");
    r->edge = e;
    return l;
}

void _r_addReader(Receptor *r,Stream *st,ReceptorAddress to,Aspect aspect,Symbol carrier,Symbol result_symbol,bool conversation) {

    // code is something like:
    // (do (not stream eof) (send to (read_stream stream line)))

    T *p,*code = NULL;
    if (conversation) {
        code = _t_new_root(CONVERSE);
        p = _t_newr(code,SCOPE);
        p = _t_newr(p,ITERATE);
    }
    else {
        code = p = _t_new_root(ITERATE);
    }

    T *params = _t_newr(p,PARAMS);
    T *eof = _t_newr(p,STREAM_ALIVE);

    _t_new_cptr(eof,EDGE_STREAM,st);
    //    _t_newi(p,TEST_INT_SYMBOL,2);  // two repetitions
    T *say = _t_newr(p,SAY);

    __r_make_addr(say,TO_ADDRESS,to);
    _t_news(say,ASPECT_IDENT,aspect);
    _t_news(say,CARRIER,carrier);

    T *s = _t_new(say,STREAM_READ,0,0);
    _t_new_cptr(s,EDGE_STREAM,st);
    _t_new(s,RESULT_SYMBOL,&result_symbol,sizeof(Symbol));

    T *run_tree = __p_build_run_tree(code,0);
    _t_free(code);
    _p_addrt2q(r->q,run_tree);
}

void _r_addWriter(Receptor *r,Stream *st,Aspect aspect) {

    T *expect = _t_new_root(PATTERN);

    char *stx = "/<LINE:LINE>";

    // @fixme for some reason parseSemtrex doesn't clean up after itself
    // valgrind reveals that some of the state in the FSA that match the
    // semtrex are left un freed.  So I'm doing this one manually below.
    /* T *t = parseSemtrex(&r->defs,stx); */
    /*  _t_add(expect,t); */

    //    T *t =_t_news(expect,SEMTREX_GROUP,NULL_SYMBOL);
    T *t =_t_newr(expect,SEMTREX_SYMBOL_ANY);
    //    _t_news(x,SEMTREX_SYMBOL,LINE);

    /* char buf[1000]; */
    /* _dump_semtrex(r->sem,t,buf); */
    /* puts(buf); */

    T* params = _t_new_root(PARAMS);
    _t_new_cptr(params,EDGE_STREAM,st);
    T* s = _t_newr(params,SLOT);
    _t_news(s,USAGE,NULL_SYMBOL);

    Symbol echo2stream;
    _sem_get_by_label(G_sem,"echo2stream",&echo2stream);

    T *act = _t_newp(0,ACTION,echo2stream);

    _r_add_expectation(r,aspect,NULL_SYMBOL,expect,act,params,0,NULL,NULL);

}

void _r_defineClockReceptor(SemTable *sem) {
    Context clk_ctx =  _d_get_receptor_context(sem,CLOCK_RECEPTOR);
    T *resp = _t_new_root(RESPOND);
    int p[] = {SignalMessageIdx,MessageHeadIdx,HeadCarrierIdx,TREE_PATH_TERMINATOR};
    _t_new(resp,SIGNAL_REF,p,sizeof(int)*4);

    Xaddr x = {TICK,1};
    T *g = _t_newr(resp,GET);
    _t_new(g,WHICH_XADDR,&x,sizeof(Xaddr));
    T *signature = __p_make_signature("result",SIGNATURE_SYMBOL,NULL_SYMBOL,NULL);
    Process proc = _d_define_process(sem,resp,"respond with current time","long desc...",signature,NULL,clk_ctx);
    T *act = _t_newp(0,ACTION,proc);
    T *pattern = _t_new_root(PATTERN);
    T *s = _sl(pattern,CLOCK_TELL_TIME);

    T *req_act = _t_newp(0,ACTION,time_request);

    T *def = _o_make_protocol_def(sem,clk_ctx,"time",
                                  ROLE,TIME_TELLER,
                                  ROLE,TIME_HEARER,
                                  GOAL,RESPONSE_HANDLER,
                                  INTERACTION,tell_time,
                                  INITIATE,TIME_HEARER,TIME_TELLER,req_act,
                                  EXPECT,TIME_TELLER,TIME_HEARER,pattern,act,NULL_SYMBOL,
                                  NULL_SYMBOL
                                  );
    Protocol tt = _d_define_protocol(sem,def,clk_ctx);

}

Receptor *_r_makeClockReceptor(SemTable *sem) {
    Receptor *r = _r_new(sem,CLOCK_RECEPTOR);

    // add the expectation in explicitly because we haven't yet defined the clock protocol
    T *expect = _t_new_root(PATTERN);
    T *s = _sl(expect,CLOCK_TELL_TIME);

    T *tick = __r_make_tick();  // initial tick, will get updated by clock thread.
    Xaddr x = _r_new_instance(r,tick);

    Protocol time;
    __sem_get_by_label(sem,"time",&time,r->context);
    _o_express_role(r,time,TIME_TELLER,DEFAULT_ASPECT,NULL);


    /* Process proc;
       __sem_get_by_label(sem,"respond with current time",&proc,r->context); */

    /* T *act = _t_newp(0,ACTION,proc); */
    /* T *params = _t_new_root(PARAMS); */
    /* _r_add_expectation(r,DEFAULT_ASPECT,CLOCK_TELL_TIME,expect,act,params,0,NULL,NULL); */

    return r;
}

/**
    bad implementation of the clock receptor thread (but easy):
   - wake up every second
   - build a TICK symbol based on the current time.
   - set the Xaddr of the current tick

  @todo: a better implementation would be to analyze the semtrex expectations that have been planted
  and only wakeup when needed based on those semtrexes

 *
 * @param[in] the clock receptor
 */
void *___clock_thread(void *arg){
    Receptor *r = (Receptor*)arg;
    debug(D_CLOCK,"clock started\n");
    int err =0;
    ReceptorAddress self = __r_get_self_address(r);
    while (r->state == Alive) {
        T *tick =__r_make_tick();
        debug(D_CLOCK,"%s\n",_td(r,tick));
        Xaddr x = {TICK,1};
        _r_set_instance(r,x,tick);
        //        T *signal = __r_make_signal(self,self,DEFAULT_ASPECT,TICK,tick,0,0,0);
        //        _r_deliver(r,signal);
        sleep(1);
        /// @todo this will skip some seconds over time.... make more sophisticated
        //       with nano-sleep so that we get every second?
    }
    debug(D_CLOCK,"clock stopped\n");
    pthread_exit(&err); //@todo determine if we should use pthread_exit or just return 0
    return 0;
}

T * __r_make_timestamp(Symbol s,int delta) {
    struct tm t;
    time_t clock;
    time(&clock);
    clock += delta;
    gmtime_r(&clock, &t);
    T *tick = _t_new_root(s);
    T *today = _t_newr(tick,TODAY);
    T *now = _t_newr(tick,NOW);
    _t_newi(today,YEAR,t.tm_year+1900);
    _t_newi(today,MONTH,t.tm_mon+1);
    _t_newi(today,DAY,t.tm_mday);
    _t_newi(now,HOUR,t.tm_hour);
    _t_newi(now,MINUTE,t.tm_min);
    _t_newi(now,SECOND,t.tm_sec);
    return tick;
}

void __r_kill(Receptor *r) {
    r->state = Dead;
    /* pthread_mutex_lock(&shutdownMutex); */
    /* G_shutdown = val; */
    /* pthread_mutex_unlock(&shutdownMutex); */
}

ReceptorAddress __r_get_self_address(Receptor *r) {
    return r->addr;
}

void __r_dump_instances(Receptor *r) {
    printf("\nINSTANCES:%s\n",_t2s(r->sem,r->instances));
}
/** @}*/
