// Observe actual LLVM managed registration/destruction; WAST owns expectations.
#include "llvm/Support/ManagedStatic.h"
#include "llvm/Support/Threading.h"
#include <cstdint>
#include <cstdlib>
#include <type_traits>
#include <unistd.h>

static unsigned Events[32], EventCount;
static unsigned DependencyCreated, DependencyDestroyed, LastDependencyValue;
static unsigned OwnerCreated, OwnerDestroyed, PlainCreated, PlainDestroyed;
static unsigned CustomCreated, CustomDeleted, ArrayCreated, ArrayDeleted;
static unsigned ArrayElementsCreated, ArrayElementsDestroyed;
static void event(unsigned Value) { Events[EventCount++] = Value; }

struct Dependency {
  unsigned Value = 41;
  Dependency() { ++DependencyCreated; event(1); }
  ~Dependency() {
    LastDependencyValue = Value;
    ++DependencyDestroyed;
    event(3);
  }
};
static llvm::ManagedStatic<Dependency> Dep;
struct Owner {
  unsigned InitialValue;
  Owner() : InitialValue(Dep->Value) { ++OwnerCreated; event(2); }
  ~Owner() { ++Dep->Value; ++OwnerDestroyed; event(4); }
};
static llvm::ManagedStatic<Owner> Root;
struct Plain {
  unsigned Value = 73;
  Plain() { ++PlainCreated; event(5); }
  ~Plain() { ++PlainDestroyed; event(6); }
};
static llvm::ManagedStatic<Plain> Leaf;
struct CustomCreator {
  static void *call() {
    ++CustomCreated;
    auto *Value = static_cast<unsigned *>(std::malloc(sizeof(unsigned)));
    *Value = 97;
    return Value;
  }
};
struct CustomDeleter {
  static void call(void *Value) { ++CustomDeleted; std::free(Value); }
};
static llvm::ManagedStatic<unsigned, CustomCreator, CustomDeleter> Custom;
struct Element {
  unsigned Value = ++ArrayElementsCreated;
  ~Element() { ++ArrayElementsDestroyed; }
};
struct ArrayCreator {
  static void *call() { ++ArrayCreated; return new Element[3]; }
};
struct ArrayDeleter {
  static void call(void *Value) {
    ++ArrayDeleted;
    llvm::object_deleter<Element[3]>::call(Value);
  }
};
static llvm::ManagedStatic<Element, ArrayCreator, ArrayDeleter> Array;

extern "C" int main(int argc, char **argv, char **environment) {
  (void)argc; (void)argv; (void)environment;
  uint32_t Output[128] = {};
  unsigned Count = 0;
  auto record = [&](unsigned Value) { Output[Count++] = Value; };
  record(LLVM_WASTE_GUEST);
  record(LLVM_ENABLE_THREADS);
  record(llvm::llvm_is_multithreaded());
  record(_LIBCPP_HAS_THREADS);
  record(std::is_trivially_destructible<llvm::ManagedStaticBase>::value);
  record(std::is_trivially_destructible<decltype(Root)>::value);

  // Empty shutdown is inert; all global handles start unconstructed.
  llvm::llvm_shutdown();
  llvm::llvm_shutdown();
  record(EventCount);
  record(Dep.isConstructed());
  record(Root.isConstructed());
  record(Leaf.isConstructed());

  // Constructing the owner lazily constructs its dependency first.
  auto *First = Root.operator->();
  record(First->InitialValue);
  record(Dep.isConstructed());
  record(Root.isConstructed());
  record(Leaf.isConstructed());
  record(DependencyCreated);
  record(OwnerCreated);
  record(EventCount);
  record(Events[0]);
  record(Events[1]);
  const auto &ConstRoot = Root;
  const auto &ConstDep = Dep;
  record(ConstRoot.operator->() == First && &*Root == First);
  record(&*ConstDep == &*Dep);
  record(DependencyCreated);
  record(OwnerCreated);

  // Explicitly destroy/recreate only the most recently registered handle.
  record(Leaf->Value);
  record(Leaf.isConstructed());
  Leaf.destroy();
  record(Leaf.isConstructed());
  record(PlainDestroyed);
  record(Root.isConstructed());
  record(Leaf->Value);
  record(PlainCreated);
  record(EventCount);

  // Reverse destruction keeps the dependency alive for its owner's destructor.
  llvm::llvm_shutdown();
  record(Dep.isConstructed());
  record(Root.isConstructed());
  record(Leaf.isConstructed());
  record(DependencyDestroyed);
  record(OwnerDestroyed);
  record(PlainDestroyed);
  record(LastDependencyValue);
  record(EventCount);
  for (unsigned I = 2; I != 8; ++I) record(Events[I]);
  llvm::llvm_shutdown();
  record(EventCount);

  // A fresh registry cycle also works via the scoped shutdown helper.
  {
    llvm::llvm_shutdown_obj Shutdown;
    record(Root->InitialValue);
    record(DependencyCreated);
    record(OwnerCreated);
  }
  record(DependencyDestroyed);
  record(OwnerDestroyed);
  record(LastDependencyValue);
  record(Dep.isConstructed());
  record(Root.isConstructed());
  record(EventCount);
  for (unsigned I = 8; I != 12; ++I) record(Events[I]);

  // Custom allocation/deletion and array deletion use real guest providers.
  record(Custom.isConstructed());
  record(*Custom);
  record(*Custom);
  record(CustomCreated);
  record(CustomDeleted);
  record(Array.isConstructed());
  auto *Elements = Array.operator->();
  for (unsigned I = 0; I != 3; ++I) record(Elements[I].Value);
  record(ArrayCreated);
  record(ArrayElementsCreated);
  llvm::llvm_shutdown();
  record(Custom.isConstructed());
  record(Array.isConstructed());
  record(CustomDeleted);
  record(ArrayDeleted);
  record(ArrayElementsDestroyed);
  llvm::llvm_shutdown();
  record(CustomDeleted);
  record(ArrayDeleted);

  // claim transfers object lifetime; shutdown must not delete it twice.
  auto *Owned = Leaf.operator->();
  record(PlainCreated);
  auto *Claimed = Leaf.claim();
  record(Claimed == Owned);
  record(Leaf.isConstructed());
  record(Claimed->Value);
  record(PlainDestroyed);
  delete Claimed;
  record(PlainDestroyed);
  llvm::llvm_shutdown();
  record(PlainDestroyed);
  record(Leaf.isConstructed());
  record(Leaf->Value);
  record(PlainCreated);
  llvm::llvm_shutdown();
  record(PlainDestroyed);
  record(Leaf.isConstructed());
  return write(1, Output, Count * sizeof(Output[0])) < 0;
}
