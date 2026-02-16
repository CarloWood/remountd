# C++ formatting

* Use East-const everywhere.
* Put opening braces on a new line.
* Member variables end on a '_'.

For example,
```
    Foo const* Bar::get_foo()
    {
      return foo_;
    }
```

* All functions use snake\_case.
* Put statements on their own line, without braces if it is a single statement.

For example,
```
    if (boolean_expression)
      new_statement();
```
The only exceptions are accessors that simply return a member:
```
    int get() const { return fd_; }
```
