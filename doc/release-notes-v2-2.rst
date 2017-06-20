=========================
Duktape 2.2 release notes
=========================

Release overview
================

Main changes in this release (see RELEASES.rst for full details):

* TBD.

Upgrading from Duktape 2.1
==========================

No action (other than recompiling) should be needed for most users to upgrade
from Duktape v2.1.x.  Note the following:

* duk_safe_call() no longer automatically extends the value stack to ensure
  there's space for 'nrets' return values.  This was not guaranteed by the
  API and the check is mostly unnecessary overhead.  If a duk_safe_call()
  call site now fails due to this change, simply ``duk_require_stack()``
  to ensure that there's reserve for ``nargs - nrets`` more elements
  (conceptually 'nargs' arguments are consumed, and 'nrets' result values
  pushed).

* Call stack (including both activation records and catcher records) is no
  longer a resized monolithic allocation which improves memory behavior for
  very low memory targets.  If you're using a pool allocator, you may need to
  measure and adjust pool sizes/counts.

* Function.prototype.call(), Function.prototype.apply(), and Reflect.apply()
  are now handled inline in call handling.  As a result, when functions are
  called via .call()/.apply() the .call()/.apply() is not part of the call
  stack and is absent in e.g. tracebacks.  .call()/.apply() no longer prevents
  a yield, doesn't consume native stack for Ecmascript-to-Ecmascript calls,
  and can now be used in tailcall positions, e.g. in
  'return func.call(null, 1, 2);'.

* Constructor calls, i.e. 'new Xyz()' or duk_new(), no longer prevent a yield,
  don't consume native stack for Ecmascript-to-Ecmascript calls, and can now
  be used in tailcalls.

* Functions pushed using duk_push_c_function() and duk_push_c_lightfunc() now
  inherit from an intermediate prototype (func -> %NativeFunctionPrototype%
  -> Function.prototype) which provides ``.name`` and ``.length`` getters.
  The setters are intentionally missing so direct writes for these properties
  fail, but you can write them using ``Object.defineProperty()`` or
  ``duk_def_prop()``.  The inherited getters can also be replaced if necessary.
  The intermediate prototype doesn't have a named global binding, but you can
  access it by reading the prototype of a pushed function.

* The bound 'this', bound arguments, and target of a duk_hboundfunc are no
  longer internal properties (but duk_hboundfunc struct members).  The 'this'
  binding, target, and bound argument count are now visible as artificial
  properties; the bound argument values are not visible in the debugger
  protocol for now.

Other minor differences:

* When an Error instance is being constructed and Duktape.errCreate() is
  called for the constructor return value, the call stack seen by errCreate()
  now includes the constructor call (previously it was unwound before calling
  errCreate()).  This affects e.g. any Duktape.act() calls in errCreate().
