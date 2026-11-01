# Memory Management in Modern C++

RAII (Resource Acquisition Is Initialization) ties the lifetime of a resource to
the lifetime of an object. When the object goes out of scope its destructor runs
and releases the resource, so files, locks and memory are freed automatically
even when an exception is thrown.

std::unique_ptr expresses exclusive ownership of a heap object. It cannot be
copied, only moved, and it deletes the object when it is destroyed. Prefer
std::make_unique to create one.

std::shared_ptr expresses shared ownership using reference counting. The object
is deleted when the last shared_ptr pointing to it is destroyed. Use
std::weak_ptr to break reference cycles between shared_ptr objects.

Avoid raw new and delete in application code. The rule of zero says that a class
which only holds RAII members does not need a user-written destructor, copy
constructor or assignment operator.
