// Generated with c2chapel version 0.1.0

// Header given to c2chapel:
require "nestedUnion.h";

// Note: Generated with fake std headers

use CPtr;
use SysCTypes;
use SysBasic;
extern "union first" union first {
  var a : c_int;
  var b : c_string;
}

extern "union second" union second {
  var a : c_ptr(c_int);
  var b : c_ptr(c_int);
}

extern "union Outer" union Outer {
  var unionField : first;
  var fieldPtr : c_ptr(second);
}

