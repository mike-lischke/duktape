/*
 *  Calls.
 *
 *  Protected variants should avoid ever throwing an error.  Must be careful
 *  to catch errors related to value stack manipulation and property lookup,
 *  not just the call itself.
 *
 *  The only exception is when arguments are insane, e.g. nargs/nrets are out
 *  of bounds; in such cases an error is thrown for two reasons.  First, we
 *  can't always respect the value stack input/output guarantees in such cases
 *  so the caller would end up with the value stack in an unexpected state.
 *  Second, an attempt to create an error might itself fail (although this
 *  could be avoided by pushing a preallocated object/string or a primitive
 *  value).
 */

#include "duk_internal.h"

/*
 *  Helpers
 */

struct duk__pcall_prop_args {
	duk_idx_t obj_idx;
	duk_idx_t nargs;
	duk_small_uint_t call_flags;
};
typedef struct duk__pcall_prop_args duk__pcall_prop_args;

struct duk__pcall_method_args {
	duk_idx_t nargs;
	duk_small_uint_t call_flags;
};
typedef struct duk__pcall_method_args duk__pcall_method_args;

struct duk__pcall_args {
	duk_idx_t nargs;
	duk_small_uint_t call_flags;
};
typedef struct duk__pcall_args duk__pcall_args;

/* Compute and validate idx_func for a certain 'nargs' and 'other'
 * parameter count (1 or 2, depending on whether 'this' binding is
 * present).
 */
DUK_LOCAL duk_idx_t duk__call_get_idx_func(duk_context *ctx, duk_idx_t nargs, duk_idx_t other) {
	duk_idx_t idx_func;

	/* XXX: byte arithmetic? */

	DUK_ASSERT(other >= 0);

	idx_func = duk_get_top(ctx) - nargs - other;
	if (DUK_UNLIKELY((idx_func | nargs) < 0)) {  /* idx_func < 0 || nargs < 0; OR sign bits */
		DUK_ERROR_TYPE_INVALID_ARGS((duk_hthread *) ctx);
		/* unreachable */
	}
	DUK_ASSERT(duk_is_valid_index(ctx, idx_func));
	return idx_func;
}

/* Compute idx_func, assume index will be valid.  This is a valid assumption
 * for protected calls: nargs < 0 is checked explicitly and duk_safe_call()
 * validates the argument count.
 */
DUK_LOCAL duk_idx_t duk__call_get_idx_func_unvalidated(duk_context *ctx, duk_idx_t nargs, duk_idx_t other) {
	duk_idx_t idx_func;

	/* XXX: byte arithmetic? */

	DUK_ASSERT(nargs >= 0);
	DUK_ASSERT(other >= 0);

	idx_func = duk_get_top(ctx) - nargs - other;
	DUK_ASSERT(idx_func >= 0);
	DUK_ASSERT(duk_is_valid_index(ctx, idx_func));
	return idx_func;
}

/* Prepare value stack for a method call through an object property.
 * May currently throw an error e.g. when getting the property.
 */
DUK_LOCAL void duk__call_prop_prep_stack(duk_context *ctx, duk_idx_t normalized_obj_idx, duk_idx_t nargs) {
	DUK_ASSERT_CTX_VALID(ctx);
	DUK_ASSERT(nargs >= 0);

	DUK_DDD(DUK_DDDPRINT("duk__call_prop_prep_stack, normalized_obj_idx=%ld, nargs=%ld, stacktop=%ld",
	                     (long) normalized_obj_idx, (long) nargs, (long) duk_get_top(ctx)));

	/* [... key arg1 ... argN] */

	/* duplicate key */
	duk_dup(ctx, -nargs - 1);  /* Note: -nargs alone would fail for nargs == 0, this is OK */
	(void) duk_get_prop(ctx, normalized_obj_idx);

	DUK_DDD(DUK_DDDPRINT("func: %!T", (duk_tval *) duk_get_tval(ctx, -1)));

	/* [... key arg1 ... argN func] */

	duk_replace(ctx, -nargs - 2);

	/* [... func arg1 ... argN] */

	duk_dup(ctx, normalized_obj_idx);
	duk_insert(ctx, -nargs - 1);

	/* [... func this arg1 ... argN] */
}

DUK_EXTERNAL void duk_call(duk_context *ctx, duk_idx_t nargs) {
	duk_hthread *thr = (duk_hthread *) ctx;
	duk_small_uint_t call_flags;
	duk_idx_t idx_func;

	DUK_ASSERT_CTX_VALID(ctx);
	DUK_ASSERT(thr != NULL);

	idx_func = duk__call_get_idx_func(ctx, nargs, 1);
	DUK_ASSERT(duk_is_valid_index(ctx, idx_func));

	duk_insert_undefined(ctx, idx_func + 1);

	call_flags = 0;  /* not protected, respect reclimit, not constructor */
	duk_handle_call_unprotected(thr, idx_func, call_flags);
}

DUK_EXTERNAL void duk_call_method(duk_context *ctx, duk_idx_t nargs) {
	duk_hthread *thr = (duk_hthread *) ctx;
	duk_small_uint_t call_flags;
	duk_idx_t idx_func;

	DUK_ASSERT_CTX_VALID(ctx);
	DUK_ASSERT(thr != NULL);

	idx_func = duk__call_get_idx_func(ctx, nargs, 2);
	DUK_ASSERT(duk_is_valid_index(ctx, idx_func));

	call_flags = 0;  /* not protected, respect reclimit, not constructor */
	duk_handle_call_unprotected(thr, idx_func, call_flags);
}

DUK_EXTERNAL void duk_call_prop(duk_context *ctx, duk_idx_t obj_idx, duk_idx_t nargs) {
	duk_hthread *thr = (duk_hthread *) ctx;

	/*
	 *  XXX: if duk_handle_call() took values through indices, this could be
	 *  made much more sensible.  However, duk_handle_call() needs to fudge
	 *  the 'this' and 'func' values to handle bound functions, which is now
	 *  done "in-place", so this is not a trivial change.
	 */

	DUK_ASSERT_CTX_VALID(ctx);

	obj_idx = duk_require_normalize_index(ctx, obj_idx);  /* make absolute */
	if (DUK_UNLIKELY(nargs < 0)) {
		DUK_ERROR_TYPE_INVALID_ARGS(thr);
	}

	duk__call_prop_prep_stack(ctx, obj_idx, nargs);

	duk_call_method(ctx, nargs);
}

DUK_LOCAL duk_ret_t duk__pcall_raw(duk_context *ctx, void *udata) {
	duk_hthread *thr = (duk_hthread *) ctx;
	duk__pcall_args *args;
	duk_idx_t idx_func;
	duk_int_t ret;

	DUK_ASSERT_CTX_VALID(ctx);
	DUK_ASSERT(udata != NULL);

	args = (duk__pcall_args *) udata;
	idx_func = duk__call_get_idx_func_unvalidated(ctx, args->nargs, 1);
	DUK_ASSERT(duk_is_valid_index(ctx, idx_func));

	duk_insert_undefined(ctx, idx_func + 1);

	ret = duk_handle_call_unprotected(thr, idx_func, args->call_flags);
	DUK_ASSERT(ret == 0);
	DUK_UNREF(ret);

	return 1;
}

DUK_EXTERNAL duk_int_t duk_pcall(duk_context *ctx, duk_idx_t nargs) {
	duk_hthread *thr = (duk_hthread *) ctx;
	duk__pcall_args args;

	DUK_ASSERT_CTX_VALID(ctx);

	args.nargs = nargs;
	if (DUK_UNLIKELY(nargs < 0)) {
		DUK_ERROR_TYPE_INVALID_ARGS(thr);
		return DUK_EXEC_ERROR;  /* unreachable */
	}
	args.call_flags = 0;

	return duk_safe_call(ctx, duk__pcall_raw, (void *) &args /*udata*/, nargs + 1 /*nargs*/, 1 /*nrets*/);
}

DUK_LOCAL duk_ret_t duk__pcall_method_raw(duk_context *ctx, void *udata) {
	duk_hthread *thr = (duk_hthread *) ctx;
	duk__pcall_method_args *args;
	duk_idx_t idx_func;
	duk_int_t ret;

	DUK_ASSERT_CTX_VALID(ctx);
	DUK_ASSERT(udata != NULL);

	args = (duk__pcall_method_args *) udata;

	idx_func = duk__call_get_idx_func_unvalidated(ctx, args->nargs, 2);
	DUK_ASSERT(duk_is_valid_index(ctx, idx_func));

	ret = duk_handle_call_unprotected(thr, idx_func, args->call_flags);
	DUK_ASSERT(ret == 0);
	DUK_UNREF(ret);

	return 1;
}

DUK_INTERNAL duk_int_t duk_pcall_method_flags(duk_context *ctx, duk_idx_t nargs, duk_small_uint_t call_flags) {
	duk_hthread *thr = (duk_hthread *) ctx;
	duk__pcall_method_args args;

	DUK_ASSERT_CTX_VALID(ctx);

	args.nargs = nargs;
	if (DUK_UNLIKELY(nargs < 0)) {
		DUK_ERROR_TYPE_INVALID_ARGS(thr);
		return DUK_EXEC_ERROR;  /* unreachable */
	}
	args.call_flags = call_flags;

	return duk_safe_call(ctx, duk__pcall_method_raw, (void *) &args /*udata*/, nargs + 2 /*nargs*/, 1 /*nrets*/);
}

DUK_EXTERNAL duk_int_t duk_pcall_method(duk_context *ctx, duk_idx_t nargs) {
	return duk_pcall_method_flags(ctx, nargs, 0);
}

DUK_LOCAL duk_ret_t duk__pcall_prop_raw(duk_context *ctx, void *udata) {
	duk__pcall_prop_args *args;
	duk_idx_t obj_idx;
	duk_int_t ret;

	DUK_ASSERT_CTX_VALID(ctx);
	DUK_ASSERT(udata != NULL);

	args = (duk__pcall_prop_args *) udata;

	obj_idx = duk_require_normalize_index(ctx, args->obj_idx);  /* make absolute */
	duk__call_prop_prep_stack(ctx, obj_idx, args->nargs);

	ret = duk_handle_call_unprotected_nargs((duk_hthread *) ctx, args->nargs, args->call_flags);
	DUK_ASSERT(ret == 0);
	DUK_UNREF(ret);
	return 1;
}

DUK_EXTERNAL duk_int_t duk_pcall_prop(duk_context *ctx, duk_idx_t obj_idx, duk_idx_t nargs) {
	duk_hthread *thr = (duk_hthread *) ctx;
	duk__pcall_prop_args args;

	DUK_ASSERT_CTX_VALID(ctx);

	args.obj_idx = obj_idx;
	args.nargs = nargs;
	if (DUK_UNLIKELY(nargs < 0)) {
		DUK_ERROR_TYPE_INVALID_ARGS(thr);
		return DUK_EXEC_ERROR;  /* unreachable */
	}
	args.call_flags = 0;

	return duk_safe_call(ctx, duk__pcall_prop_raw, (void *) &args /*udata*/, nargs + 1 /*nargs*/, 1 /*nrets*/);
}

DUK_EXTERNAL duk_int_t duk_safe_call(duk_context *ctx, duk_safe_call_function func, void *udata, duk_idx_t nargs, duk_idx_t nrets) {
	duk_hthread *thr = (duk_hthread *) ctx;
	duk_int_t rc;

	DUK_ASSERT_CTX_VALID(ctx);
	DUK_ASSERT(thr != NULL);

	/* nargs condition; fail if: top - bottom < nargs
	 *                      <=>  top < bottom + nargs
	 * nrets condition; fail if: end - (top - nargs) < nrets
	 *                      <=>  end - top + nargs < nrets
	 *                      <=>  end + nargs < top + nrets
	 */
	/* XXX: check for any reserve? */

	if (DUK_UNLIKELY((nargs | nrets) < 0 ||  /* nargs < 0 || nrets < 0; OR sign bits */
	                 thr->valstack_top < thr->valstack_bottom + nargs ||        /* nargs too large compared to top */
	                 thr->valstack_end + nargs < thr->valstack_top + nrets)) {  /* nrets too large compared to reserve */
		DUK_ERROR_TYPE_INVALID_ARGS(thr);
		return DUK_EXEC_ERROR;  /* unreachable */
	}

	rc = duk_handle_safe_call(thr,           /* thread */
	                          func,          /* func */
	                          udata,         /* udata */
	                          nargs,         /* num_stack_args */
	                          nrets);        /* num_stack_res */

	return rc;
}

DUK_EXTERNAL void duk_new(duk_context *ctx, duk_idx_t nargs) {
	duk_hthread *thr = (duk_hthread *) ctx;
	duk_idx_t idx_func;

	DUK_ASSERT_CTX_VALID(ctx);

	idx_func = duk__call_get_idx_func(ctx, nargs, 1);
	DUK_ASSERT(duk_is_valid_index(ctx, idx_func));

	duk_push_object(ctx);  /* default instance; internal proto updated by call handling */
	duk_insert(ctx, idx_func + 1);

	duk_handle_call_unprotected(thr, idx_func, DUK_CALL_FLAG_CONSTRUCTOR_CALL);
}

DUK_LOCAL duk_ret_t duk__pnew_helper(duk_context *ctx, void *udata) {
	duk_idx_t nargs;

	DUK_ASSERT(udata != NULL);
	nargs = *((duk_idx_t *) udata);

	duk_new(ctx, nargs);
	return 1;
}

DUK_EXTERNAL duk_int_t duk_pnew(duk_context *ctx, duk_idx_t nargs) {
	duk_hthread *thr = (duk_hthread *) ctx;
	duk_int_t rc;

	DUK_ASSERT_CTX_VALID(ctx);

	/* For now, just use duk_safe_call() to wrap duk_new().  We can't
	 * simply use a protected duk_handle_call() because pushing the
	 * default instance might throw.
	 */

	if (DUK_UNLIKELY(nargs < 0)) {
		DUK_ERROR_TYPE_INVALID_ARGS(thr);
		return DUK_EXEC_ERROR;  /* unreachable */
	}

	rc = duk_safe_call(ctx, duk__pnew_helper, (void *) &nargs /*udata*/, nargs + 1 /*nargs*/, 1 /*nrets*/);
	return rc;
}

DUK_EXTERNAL duk_bool_t duk_is_constructor_call(duk_context *ctx) {
	duk_hthread *thr = (duk_hthread *) ctx;
	duk_activation *act;

	DUK_ASSERT_CTX_VALID(ctx);
	DUK_ASSERT(thr != NULL);

	act = thr->callstack_curr;
	if (act != NULL) {
		return ((act->flags & DUK_ACT_FLAG_CONSTRUCT) != 0 ? 1 : 0);
	}
	return 0;
}

/* XXX: Make this obsolete by adding a function flag for rejecting a
 * non-constructor call automatically?
 */
DUK_INTERNAL void duk_require_constructor_call(duk_context *ctx) {
	if (!duk_is_constructor_call(ctx)) {
		DUK_ERROR_TYPE((duk_hthread *) ctx, DUK_STR_CONSTRUCT_ONLY);
	}
}

DUK_EXTERNAL duk_bool_t duk_is_strict_call(duk_context *ctx) {
	duk_hthread *thr = (duk_hthread *) ctx;
	duk_activation *act;

	/* For user code this could just return 1 (strict) always
	 * because all Duktape/C functions are considered strict,
	 * and strict is also the default when nothing is running.
	 * However, Duktape may call this function internally when
	 * the current activation is an Ecmascript function, so
	 * this cannot be replaced by a 'return 1' without fixing
	 * the internal call sites.
	 */

	DUK_ASSERT_CTX_VALID(ctx);
	DUK_ASSERT(thr != NULL);

	act = thr->callstack_curr;
	if (act != NULL) {
		return ((act->flags & DUK_ACT_FLAG_STRICT) != 0 ? 1 : 0);
	} else {
		/* Strict by default. */
		return 1;
	}
}

/*
 *  Duktape/C function magic
 */

DUK_EXTERNAL duk_int_t duk_get_current_magic(duk_context *ctx) {
	duk_hthread *thr = (duk_hthread *) ctx;
	duk_activation *act;
	duk_hobject *func;

	DUK_ASSERT_CTX_VALID(ctx);
	DUK_ASSERT(thr != NULL);

	act = thr->callstack_curr;
	if (act) {
		func = DUK_ACT_GET_FUNC(act);
		if (!func) {
			duk_tval *tv = &act->tv_func;
			duk_small_uint_t lf_flags;
			lf_flags = DUK_TVAL_GET_LIGHTFUNC_FLAGS(tv);
			return (duk_int_t) DUK_LFUNC_FLAGS_GET_MAGIC(lf_flags);
		}
		DUK_ASSERT(func != NULL);

		if (DUK_HOBJECT_IS_NATFUNC(func)) {
			duk_hnatfunc *nf = (duk_hnatfunc *) func;
			return (duk_int_t) nf->magic;
		}
	}
	return 0;
}

DUK_EXTERNAL duk_int_t duk_get_magic(duk_context *ctx, duk_idx_t idx) {
	duk_hthread *thr = (duk_hthread *) ctx;
	duk_tval *tv;
	duk_hobject *h;

	DUK_ASSERT_CTX_VALID(ctx);

	tv = duk_require_tval(ctx, idx);
	if (DUK_TVAL_IS_OBJECT(tv)) {
		h = DUK_TVAL_GET_OBJECT(tv);
		DUK_ASSERT(h != NULL);
		if (!DUK_HOBJECT_HAS_NATFUNC(h)) {
			goto type_error;
		}
		return (duk_int_t) ((duk_hnatfunc *) h)->magic;
	} else if (DUK_TVAL_IS_LIGHTFUNC(tv)) {
		duk_small_uint_t lf_flags = DUK_TVAL_GET_LIGHTFUNC_FLAGS(tv);
		return (duk_int_t) DUK_LFUNC_FLAGS_GET_MAGIC(lf_flags);
	}

	/* fall through */
 type_error:
	DUK_ERROR_TYPE(thr, DUK_STR_UNEXPECTED_TYPE);
	return 0;
}

DUK_EXTERNAL void duk_set_magic(duk_context *ctx, duk_idx_t idx, duk_int_t magic) {
	duk_hnatfunc *nf;

	DUK_ASSERT_CTX_VALID(ctx);

	nf = duk_require_hnatfunc(ctx, idx);
	DUK_ASSERT(nf != NULL);
	nf->magic = (duk_int16_t) magic;
}

/*
 *  Misc helpers
 */

/* Resolve a bound function on value stack top to a non-bound target
 * (leave other values as is).
 */
DUK_INTERNAL void duk_resolve_nonbound_function(duk_context *ctx) {
	duk_tval *tv;

	tv = DUK_GET_TVAL_NEGIDX(ctx, -1);
	if (DUK_TVAL_IS_OBJECT(tv)) {
		duk_hobject *h;

		h = DUK_TVAL_GET_OBJECT(tv);
		DUK_ASSERT(h != NULL);
		if (DUK_HOBJECT_HAS_BOUNDFUNC(h)) {
			duk_push_tval(ctx, &((duk_hboundfunc *) h)->target);
			duk_replace(ctx, -2);
#if 0
			DUK_TVAL_SET_TVAL(tv, &((duk_hboundfunc *) h)->target);
			DUK_TVAL_INCREF(thr, tv);
			DUK_HOBJECT_DECREF_NORZ(thr, h);
#endif
			/* Rely on Function.prototype.bind() on never creating a bound
			 * function whose target is not proper.  This is now safe
			 * because the target is not even an internal property but a
			 * struct member.
			 */
			DUK_ASSERT(duk_is_lightfunc(ctx, -1) || duk_is_callable(ctx, -1));
		}
	}

	/* Lightfuncs cannot be bound but are always callable and
	 * constructable.
	 */
}
