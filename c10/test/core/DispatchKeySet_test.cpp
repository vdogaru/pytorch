#include <gtest/gtest.h>

#include <unordered_set>

#include <c10/core/DispatchKeySet.h>
#include <c10/util/irange.h>

using namespace c10;

// This test exists not to be comprehensive, but to more clearly show
// what the semantics of DispatchKeySet are.
TEST(DispatchKeySet, ShowSemantics) {
  // the "CPU" dispatch key is an instance of a per-backend-functionality key.
  // It corresponds to "dense" functionality, "CPU" backend.
  // This means that it gets a dense functionality bit, and a cpu backend bit
  // set.
  auto undefined_set = DispatchKeySet();
  auto dense_cpu_set = DispatchKeySet(DispatchKey::CPU);
  ASSERT_TRUE(dense_cpu_set.has(DispatchKey::Dense));
  ASSERT_TRUE(dense_cpu_set.has_backend(BackendComponent::CPUBit));
  ASSERT_TRUE(dense_cpu_set.has(DispatchKey::CPU));

  auto dense_lazy_set = DispatchKeySet(DispatchKey::Lazy);
  ASSERT_TRUE(dense_lazy_set.has(DispatchKey::Dense));
  ASSERT_TRUE(dense_lazy_set.has_backend(BackendComponent::LazyBit));
  ASSERT_TRUE(dense_lazy_set.has(DispatchKey::Lazy));

  // You can think of "Dense/Sparse", and "CPUBit/CUDABit", as "building block"
  // dispatch keys. You are allowed to directly create keysets out of them!
  auto dense_cpu_set_from_building_blocks = DispatchKeySet(DispatchKey::Dense) |
      DispatchKeySet(BackendComponent::CPUBit);
  ASSERT_TRUE(dense_cpu_set.has(DispatchKey::Dense));
  ASSERT_TRUE(dense_cpu_set.has_backend(BackendComponent::CPUBit));
  ASSERT_TRUE(dense_cpu_set.has(DispatchKey::CPU));
  ASSERT_EQ(dense_cpu_set, dense_cpu_set_from_building_blocks);

  // Similarly, the AutogradCUDA key gets 2 bits in the keyset:
  // The "Autograd" functionality bit, and the "CUDA" backend bit
  auto autograd_cuda = DispatchKeySet(DispatchKey::AutogradCUDA);
  ASSERT_TRUE(autograd_cuda.has(DispatchKey::AutogradFunctionality));
  ASSERT_TRUE(autograd_cuda.has_backend(BackendComponent::CUDABit));

  // Because DispatchKeySet uses a condensed internal representation, you cannot
  // use it to represent the FULL cross product of backends and functionalities
  // for example:
  auto autograd_dense_cpu_cuda = DispatchKeySet(
      {DispatchKey::AutogradFunctionality,
       DispatchKey::Dense,
       DispatchKey::CUDA,
       DispatchKey::CPU});
  auto fpga = DispatchKeySet(DispatchKey::FPGA);
  auto fpga_and_cpu = DispatchKeySet({DispatchKey::FPGA, DispatchKey::CPU});
  // this keyset has all of the building block keys:
  ASSERT_TRUE(autograd_dense_cpu_cuda.has(DispatchKey::AutogradFunctionality));
  ASSERT_TRUE(autograd_dense_cpu_cuda.has(DispatchKey::Dense));
  ASSERT_TRUE(autograd_dense_cpu_cuda.has_backend(BackendComponent::CUDABit));
  ASSERT_TRUE(autograd_dense_cpu_cuda.has_backend(BackendComponent::CPUBit));

  // and it also has the "runtime" keys that correspond to the full
  // cross-product of functionality
  ASSERT_TRUE(autograd_dense_cpu_cuda.has(DispatchKey::AutogradCPU));
  ASSERT_TRUE(autograd_dense_cpu_cuda.has(DispatchKey::AutogradCPU));
  ASSERT_TRUE(autograd_dense_cpu_cuda.has(DispatchKey::CPU));
  ASSERT_TRUE(autograd_dense_cpu_cuda.has(DispatchKey::CUDA));

  // This means that there's no way to represent a keyset with, say, only
  // Autograd CUDA + Dense CPU. Instead, you should think of a keyset as
  // inheriting the full set of functionalities + backends of its keys. This
  // means that the below keysets are all indistinguishable from each other.
  ASSERT_EQ(
      autograd_dense_cpu_cuda,
      DispatchKeySet(
          {DispatchKey::AutogradCUDA,
           DispatchKey::AutogradCPU,
           DispatchKey::CUDA,
           DispatchKey::CPU}));
  ASSERT_EQ(
      autograd_dense_cpu_cuda,
      DispatchKeySet({DispatchKey::AutogradCUDA, DispatchKey::CPU}));
  ASSERT_EQ(
      autograd_dense_cpu_cuda,
      DispatchKeySet({DispatchKey::CUDA, DispatchKey::AutogradCPU}));

  // ~~~~~~~~~~ DispatchKeySet iterators ~~~~~~~~~~~

  // Iterators allow you to iterate individually through the DispatchKey's in a
  // DispatchKeySet
  auto empty_set = DispatchKeySet();
  auto t1 = empty_set.begin();
  auto t2 = empty_set.end();
  ASSERT_EQ(*empty_set.begin(), *empty_set.end());

  // However, only keys that correspond to actual runtime indices of kernels in
  // the operator table show up when you iterate through a keyset. i.e.
  // DispatchKey::Dense, and BackendComponent::CPUBit won't show up in an
  // iterator.
  auto dense_cpu_iter = dense_cpu_set.begin();
  ASSERT_EQ(*dense_cpu_iter++, DispatchKey::CPU);
  ASSERT_EQ(*dense_cpu_iter, *dense_cpu_set.end());

  auto autograd_dense_cpu_cuda_iter = autograd_dense_cpu_cuda.begin();
  ASSERT_EQ(*autograd_dense_cpu_cuda_iter++, DispatchKey::CPU);
  ASSERT_EQ(*autograd_dense_cpu_cuda_iter++, DispatchKey::CUDA);
  ASSERT_EQ(*autograd_dense_cpu_cuda_iter++, DispatchKey::AutogradCPU);
  ASSERT_EQ(*autograd_dense_cpu_cuda_iter++, DispatchKey::AutogradCUDA);
  ASSERT_EQ(*autograd_dense_cpu_cuda_iter, *autograd_dense_cpu_cuda.end());

  // But other "functionality bits" that are not defined per-backend DO get
  // their own slots in the operator table.
  auto mixed_keyset = DispatchKeySet(BackendComponent::CPUBit) |
      DispatchKeySet(
                          {DispatchKey::FPGA, // runtime key
                           DispatchKey::Functionalize, // runtime key
                           DispatchKey::Dense}); // NOT a runtime key
  auto mixed_iter = mixed_keyset.begin();
  ASSERT_EQ(*mixed_iter++, DispatchKey::CPU);
  ASSERT_EQ(*mixed_iter++, DispatchKey::FPGA);
  ASSERT_EQ(*mixed_iter++, DispatchKey::Functionalize);
  ASSERT_EQ(*mixed_iter, *mixed_keyset.end());
}

TEST(DispatchKeySet, Empty) {
  DispatchKeySet empty_set;
  for (uint8_t i = 0;
       i <= static_cast<uint8_t>(DispatchKey::EndOfRuntimeBackendKeys);
       i++) {
    auto tid = static_cast<DispatchKey>(i);
    if (tid == DispatchKey::Undefined)
      continue;
    ASSERT_FALSE(empty_set.has(tid));
  }
  ASSERT_TRUE(empty_set.empty());
  DispatchKeySet empty_set2;
  ASSERT_TRUE(empty_set == empty_set2);
}

// This covers all keys that correspond to a single backend bit, e.g.
// BackendComponent::CPUBit. Even though these are NOT runtime keys, we still
// allow adding them directly to a keyset
TEST(DispatchKeySet, SingletonBackendComponent) {
  for (const auto i : c10::irange(1, num_backends)) {
    auto tid = static_cast<DispatchKey>(i);
    DispatchKeySet sing(tid);
    ASSERT_EQ(sing, sing);
    ASSERT_EQ(sing, DispatchKeySet().add(tid));
    ASSERT_EQ(sing, sing.add(tid));
    ASSERT_EQ(sing, sing | sing);
    ASSERT_FALSE(sing.empty());
    ASSERT_TRUE(sing.has(tid));
  }
}

// This covers all keys that correspond to a single functionality bit:
// - runtime, not-per-backend functionality keys, e.g.
// DispatchKey::FuncTorchBatched
// - runtime, "fake backend" keys, e.g. DispatchKey::FPGA
// - NOT-runtime, per-backend functionality keys, e.g. DispatchKey::Dense
//   Even though it's not a runtime key, we still allow adding it directly to a
//   keyset.
// DispatchKey::
TEST(DispatchKeySet, SingletonFunctionalityKeys) {
  for (const auto i : c10::irange(1, num_functionality_keys)) {
    auto tid = static_cast<DispatchKey>(i);
    DispatchKeySet sing(tid);
    ASSERT_EQ(sing, sing);
    ASSERT_EQ(sing, DispatchKeySet().add(tid));
    ASSERT_EQ(sing, sing.add(tid));
    ASSERT_EQ(sing, sing | sing);
    ASSERT_FALSE(sing.empty());
    ASSERT_TRUE(sing.has(tid));
    ASSERT_EQ(sing.remove(tid), DispatchKeySet());
  }
}

// This covers runtime keys that are per-backend,
// and take up more than one bit in a DispatchKeySet. They take up one
// functionality bit + one backend bit. e.g. CPU, CUDA, SparseCPU, SparseCUDA,
// AutogradCPU, AutogradCUDA
TEST(DispatchKeySet, SingletonPerBackendFunctionalityKeys) {
  for (uint8_t i = static_cast<uint8_t>(DispatchKey::StartOfDenseBackends);
       i <= static_cast<uint8_t>(DispatchKey::EndOfRuntimeBackendKeys);
       i++) {
    auto tid = static_cast<DispatchKey>(i);
    // Skip these because they aren't real keys.
    if (tid == DispatchKey::StartOfDenseBackends ||
        tid == DispatchKey::StartOfSparseBackends ||
        tid == DispatchKey::StartOfQuantizedBackends ||
        tid == DispatchKey::StartOfAutogradBackends) {
      continue;
    }
    DispatchKeySet sing(tid);
    ASSERT_EQ(sing, sing);
    ASSERT_EQ(sing, DispatchKeySet().add(tid));
    ASSERT_EQ(sing, sing.add(tid));
    ASSERT_EQ(sing, sing | sing);
    ASSERT_FALSE(sing.empty());
    ASSERT_TRUE(sing.has(tid));

    auto functionality_key = toFunctionalityKey(tid);
    auto backend_key = toBackendComponent(tid);
    // These two sets should be equivalent:
    // DispatchKeySet(DispatchKey::CPU)
    // DispatchKeySet({DispatchKey::Dense, BackendComponent::CPUBit})
    auto expected_ks =
        DispatchKeySet(functionality_key) | DispatchKeySet(backend_key);
    ASSERT_EQ(sing, expected_ks);
    // These two sets should be equivalent:
    // DispatchKeySet(DispatchKey::CPU).remove(DispatchKey::Dense)
    // DispatchKeySet(BackendComponent::CPUBit)
    expected_ks = DispatchKeySet(toBackendComponent(tid));
    ASSERT_EQ(sing.remove(tid), expected_ks);
  }
}

TEST(DispatchKeySet, DoubletonPerBackend) {
  for (uint8_t i = static_cast<uint8_t>(DispatchKey::StartOfDenseBackends);
       i <= static_cast<uint8_t>(DispatchKey::EndOfRuntimeBackendKeys);
       i++) {
    for (uint8_t j = i + 1;
         j <= static_cast<uint8_t>(DispatchKey::EndOfRuntimeBackendKeys);
         j++) {
      ASSERT_LT(i, j);
      auto tid1 = static_cast<DispatchKey>(i);
      auto tid2 = static_cast<DispatchKey>(j);

      // Skip these because they aren't real keys.
      if (tid1 == DispatchKey::StartOfDenseBackends ||
          tid1 == DispatchKey::StartOfSparseBackends ||
          tid1 == DispatchKey::StartOfQuantizedBackends ||
          tid1 == DispatchKey::StartOfAutogradBackends)
        continue;
      if (tid2 == DispatchKey::StartOfDenseBackends ||
          tid2 == DispatchKey::StartOfSparseBackends ||
          tid2 == DispatchKey::StartOfQuantizedBackends ||
          tid2 == DispatchKey::StartOfAutogradBackends)
        continue;

      auto backend1 = toBackendComponent(tid1);
      auto backend2 = toBackendComponent(tid2);
      auto functionality1 = toFunctionalityKey(tid1);
      auto functionality2 = toFunctionalityKey(tid2);

      auto combined = DispatchKeySet({tid1, tid2});
      // The combined set has the backend bits
      ASSERT_TRUE(combined.has_backend(backend1));
      ASSERT_TRUE(combined.has_backend(backend2));
      // and it has the backend bits
      ASSERT_TRUE(combined.has(functionality1));
      ASSERT_TRUE(combined.has(functionality2));
      // and it has the original two runtime keys
      ASSERT_TRUE(combined.has(tid1));
      ASSERT_TRUE(combined.has(tid2));

      // Add all of the keys in the keyset to a real set
      std::unordered_set<DispatchKey> visited_keys;
      auto iter = combined.begin();
      while (*iter != *combined.end()) {
        visited_keys.insert(*iter);
        ++iter;
      }
      std::unordered_set<DispatchKey> expected_keys;
      expected_keys.insert(
          toRuntimePerBackendFunctionalityKey(functionality1, backend1));
      expected_keys.insert(
          toRuntimePerBackendFunctionalityKey(functionality1, backend2));
      expected_keys.insert(
          toRuntimePerBackendFunctionalityKey(functionality2, backend1));
      expected_keys.insert(
          toRuntimePerBackendFunctionalityKey(functionality2, backend2));
      ASSERT_EQ(expected_keys, visited_keys);

      if (backend1 == backend2 || functionality1 == functionality2) {
        // We have two runtime keys, with either the same backend or the same
        // per-backend functionalities. E.g. {AutogradCUDA, CUDA} or
        // {AutogradCPU, AutogradCUDA} There should be 2 total runtime keys in
        // this set.
        ASSERT_EQ(2, visited_keys.size());
      } else {
        // since i and j are different keys, they should not have the same
        // functionality and backend
        ASSERT_TRUE(backend1 != backend2 && functionality1 != functionality2);
        // We have two runtime keys, that have different backends + per-backend
        // functionalities. So we should expect the full cross product of
        // runtime keys to be in the set. e.g. if i = AutogradCUDA, and j = CPU,
        // then combined = {AutogradCUDA, AutogradCPU, CUDA, CPU}
        ASSERT_EQ(4, visited_keys.size());
      }
    }
  }
}

TEST(DispatchKeySet, Full) {
  DispatchKeySet full(DispatchKeySet::FULL);
  for (const auto i : c10::irange(1, num_functionality_keys)) {
    auto tid = static_cast<DispatchKey>(i);
    ASSERT_TRUE(full.has(tid));
  }
  ASSERT_FALSE(full.has(DispatchKey::EndOfFunctionalityKeys));
}

TEST(DispatchKeySet, IteratorBasicOps) {
  DispatchKeySet empty_set;
  DispatchKeySet full_set(DispatchKeySet::FULL);
  DispatchKeySet mutated_set = empty_set.add(DispatchKey::CPU);

  // Constructor + Comparison
  ASSERT_EQ(*empty_set.begin(), DispatchKey::EndOfFunctionalityKeys);
  ASSERT_EQ(*empty_set.end(), DispatchKey::EndOfFunctionalityKeys);
  ASSERT_EQ(*mutated_set.begin(), DispatchKey::CPU);

  ASSERT_TRUE(empty_set.begin() == empty_set.end());
  ASSERT_TRUE(full_set.begin() != full_set.end());

  // Increment Ops
  ASSERT_TRUE(full_set.begin() == full_set.begin()++);
  ASSERT_TRUE(full_set.begin() != ++full_set.begin());
}

TEST(DispatchKeySet, IteratorEmpty) {
  DispatchKeySet empty_set;
  uint8_t i = 0;

  for (auto it = empty_set.begin(); it != empty_set.end(); ++it) {
    i++;
  }
  ASSERT_EQ(i, 0);
}

TEST(DispatchKeySet, IteratorCrossProduct) {
  // The iterator should return all runtime keys in the set,
  // including the cross product of {backends} x {functionalities}
  auto ks =
      DispatchKeySet({BackendComponent::CPUBit, BackendComponent::CUDABit}) |
      DispatchKeySet(
          {DispatchKey::Dense,
           DispatchKey::FPGA,
           DispatchKey::AutogradFunctionality});

  auto iter = ks.begin();
  // iterate through dense backends first.
  ASSERT_EQ(DispatchKey::CPU, *(iter++));
  ASSERT_EQ(DispatchKey::CUDA, *(iter++));
  // FPGA doesn't have a backend bit, so it isn't included in the cross product.
  ASSERT_EQ(DispatchKey::FPGA, *(iter++));
  // iterate through the autograd keys laster.
  ASSERT_EQ(DispatchKey::AutogradCPU, *(iter++));
  ASSERT_EQ(DispatchKey::AutogradCUDA, *(iter++));
}

TEST(DispatchKeySet, IteratorFull) {
  DispatchKeySet full_set(DispatchKeySet::FULL);
  uint8_t i = 0;

  for (const auto& it : full_set) {
    i++;
  }
  // Total # of runtime entries includes an entry for DispatchKey::Undefined,
  // which is not included when iterating through the DispatchKeySet.
  ASSERT_EQ(i, num_runtime_entries - 1);
}

TEST(DispatchKeySet, IteratorRangeFull) {
  DispatchKeySet full_set(DispatchKeySet::FULL);
  uint8_t i = 0;

  for (DispatchKey dispatch_key : full_set) {
    i++;
  }

  // Total # of runtime entries includes an entry for DispatchKey::Undefined,
  // which is not included when iterating through the DispatchKeySet.
  ASSERT_EQ(i, num_runtime_entries - 1);
}

TEST(DispatchKeySet, FailAtEndIterator) {
  DispatchKeySet full_set(DispatchKeySet::FULL);
  uint64_t raw_repr = full_set.raw_repr();

  // doesn't throw
  DispatchKeySet::iterator(&raw_repr, num_backends + num_functionality_keys);
  // NOLINTNEXTLINE(cppcoreguidelines-avoid-goto,hicpp-avoid-goto)
  EXPECT_THROW(
      DispatchKeySet::iterator(
          &raw_repr, num_backends + num_functionality_keys + 1),
      c10::Error);
}

TEST(DispatchKeySet, TestKeyOrderingInvariants) {
  for (uint8_t i = static_cast<uint8_t>(DispatchKey::StartOfDenseBackends);
       i <= static_cast<uint8_t>(DispatchKey::EndOfRuntimeBackendKeys);
       i++) {
    auto k = static_cast<DispatchKey>(i);
    // Note [The Ordering of Per-Backend Dispatch Keys Matters!]
    // The DispatchKey enum includes all of the runtime keys for
    // Dense/Sparse/Quantized/Autograd, (e.g. CPU, CUDA, SparseCPU, SparseCUDA,
    // AutogradCPU, AutogradCUDA, etc). And we expect the ordering of those keys
    // to be the same as the ordering of the backends in the `BackendComponent`
    // enum. This makes several utilities in `DispatchKey.h` and
    // `DispatchKeySet.h` significantly easier to implement. The purpose of the
    // test is to assert (through CI) that this invariant is maintained.
    //
    // The only way that we can really check this invariant is by
    // comparing the string names of each enum.
    // We only really care about the ordering for "real" keys that are actually
    // used, which we expect to be able to print properly. This saves us from
    // having to enumerate the full set of possible runtime keys in
    // DispatchKey::toString(). It also relies on toString() being implemented
    // correctly.
    auto functionality_str = std::string(toString(k));
    if (functionality_str == "UNKNOWN_TENSOR_TYPE_ID")
      continue;

    auto computed_backend_k = toBackendComponent(k);
    auto computed_backend_str = std::string(toString(computed_backend_k));
    // Skip, e.g., the "Bit" from "CPUBit"
    computed_backend_str =
        computed_backend_str.substr(0, computed_backend_str.size() - 3);

    ASSERT_TRUE(
        functionality_str.find(computed_backend_str) != std::string::npos)
        << "DispatchKey invariant broken! Found a key that is not ordered correctly"
        << " with its backend bit. key = " << toString(k) << ", " << k
        << ", computed backend = " << toString(computed_backend_k);
  }
}
