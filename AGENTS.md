# Configuration

To configure run
```
    cmake -S "$REPOBASE" -B "$BUILDDIR" -DCMAKE\_BUILD\_TYPE="Debug" -DEnableDebug=OFF
```
to get debug output, remove the `-DEnableDebug=OFF`.

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

# Code Style

* Do not remove comments.
* Every member variable needs to be commented.
* Most member functions need a comment to explain what they do / return etc.
* Every class needs comments above it explaining what the class represents and expected usage.
