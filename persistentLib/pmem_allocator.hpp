#pragma once

#ifndef _PMEM_ALLOCATOR_HPP_
#define _PMEM_ALLOCATOR_HPP_


#include <iostream>
#include <libpmemobj.h>
#include <libpmemobj++/pool.hpp>
#include <libpmemobj++/transaction.hpp>
#include <unistd.h>
#include <stdexcept>


#define PMEM_POOL_SIZE (1024 * 1024 * 1024) // 1GB  
#define PMEM_POOL_PATH "/mnt/pmem-emu/global_persistent_pool"

inline PMEMobjpool *global_pool = nullptr;
inline pmem::obj::pool_base pmem_pool_handle;

// Helper function to get the pool path from environment variable or use default
inline const char* get_pool_path() {
    const char *p = getenv("PERSISTENT_POOL_PATH");
    return p ? p : PMEM_POOL_PATH;
}


__attribute__((constructor))
inline void pmem_alloc_init() {
    if (access(get_pool_path(), F_OK) == 0) {
        // Pool already exists, open it
        global_pool = pmemobj_open(get_pool_path(), "pmem_pool");
        if (global_pool == NULL) {
            throw std::runtime_error("Failed to open existing PMEM pool");  
        }
        pmem_pool_handle = pmem::obj::pool_base(global_pool);
    }
    else{
        // Pool doesn't exist, create it
        global_pool = pmemobj_create(get_pool_path(), "pmem_pool", PMEM_POOL_SIZE, 0666);
        if (global_pool == NULL) {
            throw std::runtime_error("Failed to create PMEM pool");
        }
        pmem_pool_handle = pmem::obj::pool_base(global_pool);
    }
}

__attribute__((destructor))
inline void pmem_alloc_fini() {
    if (global_pool != nullptr) {
        pmem_pool_handle = pmem::obj::pool_base{}; // Reset to default-constructed state
        pmemobj_close(global_pool);
        global_pool = nullptr;
    }

}

/* function templates are inline by default*/
template<typename T> T* pmem_root() {
    if(global_pool == nullptr) {
        throw std::runtime_error("PMEM pool is not initialized");
    }
    PMEMoid root_oid = pmemobj_root(global_pool, sizeof(T));
    return static_cast<T*>(pmemobj_direct(root_oid));
}


inline pmem::obj::pool_base& pmem_pool() {
    if(global_pool == nullptr) {
        throw std::runtime_error("PMEM pool is not initialized");
    }
    return pmem_pool_handle;
}



inline void* pmem_alloc(size_t size, size_t align){
    if(global_pool == nullptr) {
        throw std::runtime_error("PMEM pool is not initialized");
    }
    PMEMoid oid;
    if(pmemobj_tx_stage() == TX_STAGE_WORK){
        oid = pmemobj_tx_alloc(size,0);
        if (OID_IS_NULL(oid)) {
            throw std::runtime_error("Failed to allocate memory from PMEM pool (transactional)");
        }
    }else {
        int succ = pmemobj_alloc(global_pool, &oid, size, 0, NULL, NULL);
        if(succ != 0) {
            throw std::runtime_error("Failed to allocate memory from PMEM pool");
        }
    }
    return pmemobj_direct(oid);
}

inline void pmem_free(void* ptr) {
    if(global_pool == nullptr) {
        throw std::runtime_error("PMEM pool is not initialized");
    }
    PMEMoid oid = pmemobj_oid(ptr);
    if (pmemobj_tx_stage() == TX_STAGE_WORK) {
        int rc = pmemobj_tx_free(oid);
        if (rc != 0) {
            throw std::runtime_error("Failed to free memory from PMEM pool within transaction");
        }
    } else {
        pmemobj_free(&oid);
    }
}

inline bool pmem_contains(void* ptr) {
    if(global_pool == nullptr) {
        return false;
    }
    return pmemobj_pool_by_ptr(ptr) == global_pool;
}



template<typename T>
class pmem_ptr {
    private:
        PMEMoid oid_;
        void snapshot_if_pmem();  // helper to log changes to PMEM objects
    public:
        pmem_ptr() : oid_{OID_NULL} {}
        explicit pmem_ptr(T* raw): oid_{pmemobj_oid(raw)} {}
        pmem_ptr(const pmem_ptr&) = default;
        pmem_ptr(pmem_ptr&&) = default;

        // Accessors
        T* get() const {
            return static_cast<T*>(pmemobj_direct(oid_));
        }

        T& operator*() const{return *get();}
        T* operator->() const{return get();}
        explicit operator bool() const{return !OID_IS_NULL(oid_);}

        //Mutators (each will call snapshot_if_pmem before modifying the object)
        pmem_ptr& operator=(T* raw){
            snapshot_if_pmem();
            oid_ = pmemobj_oid(raw);
            return *this;
        }
        pmem_ptr& operator=(std::nullptr_t){
            snapshot_if_pmem();
            oid_ = OID_NULL;
            return *this;
        }
        pmem_ptr& operator=(const pmem_ptr& other){
            snapshot_if_pmem();
            oid_ = other.oid_;
            return *this;
        }

        //Comparison
        bool operator==(const pmem_ptr& other) const{
            return OID_EQUALS(oid_, other.oid_);
        }
        bool operator!=(const pmem_ptr& other) const{
            return !(*this == other);
        }
        bool operator==(std::nullptr_t) const{
            return OID_IS_NULL(oid_);
        }
        bool operator!=(std::nullptr_t) const{
            return !OID_IS_NULL(oid_);
        }
};

template<typename T>
void pmem_ptr<T>::snapshot_if_pmem() {
    if(pmem_contains(&oid_)) {
        int succ = pmemobj_tx_add_range_direct(&oid_, sizeof(oid_));
        if(succ != 0) {
            throw std::runtime_error("pmem_ptr assignment outside txn.");
        }
    }
}

template<typename T, typename... Args>
T* pmem_get_or_create(pmem_ptr<T>& slot, Args&&... args) {
    if(slot) return slot.get();

    T* obj = nullptr;
    pmem::obj::transaction::run(pmem_pool(), [&]{
        obj = new T(std::forward<Args>(args)...);
        slot = obj;
    });
    return obj;
}

#endif // _PMEM_ALLOCATOR_HPP_