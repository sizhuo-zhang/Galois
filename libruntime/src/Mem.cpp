/** Memory allocator implementation -*- C++ -*-
 * @file
 * @section License
 *
 * This file is part of Galois.  Galois is a framework to exploit
 * amorphous data-parallelism in irregular programs.
 *
 * Galois is free software: you can redistribute it and/or modify it
 * under the terms of the GNU Lesser General Public License as
 * published by the Free Software Foundation, either version 3 of the
 * License, or (at your option) any later version.
 *
 * Galois is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with Galois.  If not, see
 * <http://www.gnu.org/licenses/>.
 *
 * @section Copyright
 *
 * Copyright (C) 2015, The University of Texas at Austin. All rights
 * reserved.
 *
 * @section Description
 *
 * Strongly inspired by heap layers:
 *  http://www.heaplayers.org/
 * FSB is modified from:
 *  http://warp.povusers.org/FSBAllocator/
 *
 * @author Andrew Lenharth <andrewl@lenharth.org>
 */

#include "Galois/Runtime/Mem.h"

#include <map>
#include <mutex>

using namespace Galois::Runtime;

//Anchor the class
SystemHeap::SystemHeap() {
  assert(AllocSize == Runtime::pagePoolSize());
}

SystemHeap::~SystemHeap() {}

#ifndef GALOIS_FORCE_STANDALONE
Galois::Substrate::PtrLock<SizedHeapFactory> SizedHeapFactory::instance;
__thread SizedHeapFactory::HeapMap* SizedHeapFactory::localHeaps = 0;
Galois::Substrate::PtrLock<Pow_2_BlockHeap>  Pow_2_BlockHeap::instance;

SizedHeapFactory::SizedHeap* 
SizedHeapFactory::getHeapForSize(const size_t size) {
  if (size == 0)
    return 0;
  return getInstance()->getHeap(size);
}

SizedHeapFactory::SizedHeap* 
SizedHeapFactory::getHeap(const size_t size) {
  typedef SizedHeapFactory::HeapMap HeapMap;

  if (!localHeaps) {
    std::lock_guard<Galois::Substrate::SimpleLock> ll(lock);
    localHeaps = new HeapMap;
    allLocalHeaps.push_front(localHeaps);
  }

  auto& lentry = (*localHeaps)[size];
  if (lentry)
    return lentry;

  {
    std::lock_guard<Galois::Substrate::SimpleLock> ll(lock);
    auto& gentry = heaps[size];
    if (!gentry)
      gentry = new SizedHeap();
    lentry = gentry;
    return lentry;
  }
}

SizedHeapFactory* SizedHeapFactory::getInstance() {
  SizedHeapFactory* f = instance.getValue();
  if (f)
    return f;
  
  instance.lock();
  f = instance.getValue();
  if (f) {
    instance.unlock();
  } else {
    f = new SizedHeapFactory();
    instance.unlock_and_set(f);
  }
  return f;
}

Pow_2_BlockHeap::Pow_2_BlockHeap (void) throw (): heapTable () {
  populateTable ();
}

Pow_2_BlockHeap* Pow_2_BlockHeap::getInstance() {
  Pow_2_BlockHeap* f = instance.getValue();
  if (f)
    return f;
  
  instance.lock();
  f = instance.getValue();
  if (f) {
    instance.unlock();
  } else {
    f = new Pow_2_BlockHeap();
    instance.unlock_and_set(f);
  }
  return f;
}

SizedHeapFactory::SizedHeapFactory() :lock() {}

SizedHeapFactory::~SizedHeapFactory() {
  // TODO destructor ordering problem: there may be pointers to deleted
  // SizedHeap when this Factory is destroyed before dependent
  // FixedSizeHeaps.
  for (auto entry : heaps)
    delete entry.second;
  for (auto mptr : allLocalHeaps)
    delete mptr;
}
#endif