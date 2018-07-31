* empty interface:
  ```
  var t interface{}
  ```
  all types satisfy empty interface;

* top level variables assignment must be prefixed with `var` keyword, and short variable
  declarations can only be used inside function body; short variable declaration can omit
  type;

* type switch:
  ```
  var t interface{}
  switch t:=t.(type)
  ```

* variables in go is zero by default, i.e, 0 for int, 0.0 for float, "" for string, nil for pointer;
* method:
  ```
  func (c *FooStruct) fooFunc() float64 {...}
  c.fooFunc()
  ```

* embedded type, aka anonymous field, represent is-a relationship;
  ```
  type Android struct {
      Person
      Name string
  }
  ```
  methods of Person can be used directly by Android; Android is kind of Person;

* ... before a parameter type means variadic parameter
* there is no `implement` keyword, struct satisfy interface by implementing all the methods;
* byte is uint8, rune is int32, both are built-in types; int, uint, and uintptr are machine dependent types;
  go has no double type, they are float32 and float64
* go has bool type, in one bit, true and false, lower case;
* range returns two value, first one is index, second is value;
* slice is var-length array; use `make` to allocate a slice with specified length; it is a segment of array;
  `append` of slice would create a new slice;
* map must call `make` before we can use it;
  key access of map returns two values, first is value, second is whether found in the set;

* closure can be seen as another form of function level static variable implementation;
* defer would execute in panic cases, hence recover() is normally in defer style;
* go has GC;
* select blocks for channel
* Capitalized functions can be seen by other packages;
* String is a built-in type, strings is a built-in package;
* shell script to find all go files for cscope:
  ```
  find . -name "*.go" -print > cscope.files
  ```

* func (n *FooStr) FooFunc() {} here n is a pointer, but use . to access fields; in Go, pointer and struct both
  uses . to access fields, compiler knows what you mean, -> is for channel;
* type assertion:

  ```
  n = newNode.(\*InsertStmt) //actually the second ok return value is omitted
  ```

* `error` is an interface defined in package `errors`, only one method Error() defined;
