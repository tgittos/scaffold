#include "hnswlib_wrapper.h"
#include <hnswlib/hnswlib.h>
#include <unordered_map>
#include <memory>
#include <string>
#include <cstring>
#include <cmath>

using namespace hnswlib;

static std::unordered_map<std::string, std::unique_ptr<HierarchicalNSW<float>>> indexes;
static std::unordered_map<std::string, std::unique_ptr<SpaceInterface<float>>> spaces;

class L2SpaceWrapper : public L2Space {
public:
    L2SpaceWrapper(size_t dim) : L2Space(dim) {}
};

class InnerProductSpaceWrapper : public InnerProductSpace {
public:
    InnerProductSpaceWrapper(size_t dim) : InnerProductSpace(dim) {}
};

class CosineSpace : public SpaceInterface<float> {
    size_t data_size_;
    size_t dim_;

public:
    CosineSpace(size_t dim) {
        dim_ = dim;
        data_size_ = dim * sizeof(float);
    }

    size_t get_data_size() {
        return data_size_;
    }

    DISTFUNC<float> get_dist_func() {
        return CosineDistanceSIMD;
    }

    void *get_dist_func_param() {
        return &dim_;
    }

    ~CosineSpace() {}

    static float CosineDistanceSIMD(const void *pVect1v, const void *pVect2v, const void *qty_ptr) {
        float *pVect1 = (float *) pVect1v;
        float *pVect2 = (float *) pVect2v;
        size_t qty = *((size_t *) qty_ptr);

        float dot = 0.0f;
        float norm1 = 0.0f;
        float norm2 = 0.0f;

        for (size_t i = 0; i < qty; i++) {
            dot += pVect1[i] * pVect2[i];
            norm1 += pVect1[i] * pVect1[i];
            norm2 += pVect2[i] * pVect2[i];
        }

        // Avoid division by zero
        if (norm1 == 0.0f || norm2 == 0.0f) {
            return 1.0f;
        }

        // Cosine similarity = dot / (norm1 * norm2)
        // Distance = 1 - cosine_similarity
        float similarity = dot / (sqrtf(norm1) * sqrtf(norm2));
        return 1.0f - similarity;
    }
};

extern "C" {

hnswlib_index_t hnswlib_create_index(const char* name, const hnswlib_index_config_t* config) {
    try {
        std::string index_name(name);
        
        if (indexes.find(index_name) != indexes.end()) {
            return nullptr;
        }
        
        std::unique_ptr<SpaceInterface<float>> space;
        if (strcmp(config->metric, "l2") == 0) {
            space = std::make_unique<L2SpaceWrapper>(config->dimension);
        } else if (strcmp(config->metric, "ip") == 0) {
            space = std::make_unique<InnerProductSpaceWrapper>(config->dimension);
        } else if (strcmp(config->metric, "cosine") == 0) {
            space = std::make_unique<CosineSpace>(config->dimension);
        } else {
            return nullptr;
        }
        
        auto index = std::make_unique<HierarchicalNSW<float>>(
            space.get(), 
            config->max_elements,
            config->M,
            config->ef_construction,
            config->random_seed
        );
        
        spaces[index_name] = std::move(space);
        indexes[index_name] = std::move(index);
        
        return reinterpret_cast<hnswlib_index_t>(indexes[index_name].get());
    } catch (...) {
        return nullptr;
    }
}

int hnswlib_delete_index(const char* name) {
    std::string index_name(name);
    auto it = indexes.find(index_name);
    if (it == indexes.end()) {
        return -1;
    }
    
    indexes.erase(it);
    spaces.erase(index_name);
    return 0;
}

int hnswlib_has_index(const char* name) {
    return indexes.find(std::string(name)) != indexes.end() ? 1 : 0;
}

int hnswlib_add_vector(const char* name, const float* data, size_t label) {
    try {
        auto it = indexes.find(std::string(name));
        if (it == indexes.end() || !it->second) {
            return -1;
        }
        
        it->second->addPoint(data, label);
        return 0;
    } catch (...) {
        return -1;
    }
}

int hnswlib_update_vector(const char* name, const float* data, size_t label) {
    try {
        auto it = indexes.find(std::string(name));
        if (it == indexes.end() || !it->second) {
            return -1;
        }
        
        // In hnswlib, calling addPoint with an existing label updates it
        it->second->addPoint(data, label);
        
        return 0;
    } catch (...) {
        return -1;
    }
}

int hnswlib_delete_vector(const char* name, size_t label) {
    try {
        auto it = indexes.find(std::string(name));
        if (it == indexes.end() || !it->second) {
            return -1;
        }
        
        it->second->markDelete(label);
        return 0;
    } catch (...) {
        return -1;
    }
}

int hnswlib_get_vector(const char* name, size_t label, float* data) {
    try {
        auto it = indexes.find(std::string(name));
        if (it == indexes.end() || !it->second) {
            return -1;
        }
        
        std::vector<float> vec = it->second->getDataByLabel<float>(label);
        auto space_it = spaces.find(std::string(name));
        if (space_it == spaces.end() || !space_it->second) {
            return -1;
        }
        
        size_t dim = space_it->second->get_data_size() / sizeof(float);
        memcpy(data, vec.data(), dim * sizeof(float));
        return 0;
    } catch (...) {
        return -1;
    }
}

hnswlib_search_results_t* hnswlib_search(const char* name, const float* query, size_t k) {
    try {
        auto it = indexes.find(std::string(name));
        if (it == indexes.end() || !it->second) {
            return nullptr;
        }
        
        auto result = it->second->searchKnn(query, k);
        
        auto* results = new hnswlib_search_results_t;
        results->count = result.size();
        results->labels = new size_t[results->count];
        results->distances = new float[results->count];
        
        for (size_t i = 0; i < results->count; i++) {
            results->labels[results->count - 1 - i] = result.top().second;
            results->distances[results->count - 1 - i] = result.top().first;
            result.pop();
        }
        
        return results;
    } catch (...) {
        return nullptr;
    }
}

void hnswlib_free_search_results(hnswlib_search_results_t* results) {
    if (results) {
        delete[] results->labels;
        delete[] results->distances;
        delete results;
    }
}

int hnswlib_save_index(const char* name, const char* path) {
    try {
        auto it = indexes.find(std::string(name));
        if (it == indexes.end() || !it->second) {
            return -1;
        }
        
        it->second->saveIndex(std::string(path));
        return 0;
    } catch (...) {
        return -1;
    }
}

int hnswlib_load_index(const char* name, const char* path, const hnswlib_index_config_t* config) {
    try {
        if (!config) return -1;
        
        std::string index_name(name);
        
        // If index already exists, delete it first
        auto existing = indexes.find(index_name);
        if (existing != indexes.end()) {
            indexes.erase(existing);
            spaces.erase(index_name);
        }
        
        // Create space based on metric
        std::unique_ptr<SpaceInterface<float>> space;
        if (strcmp(config->metric, "ip") == 0) {
            space = std::make_unique<InnerProductSpaceWrapper>(config->dimension);
        } else if (strcmp(config->metric, "cosine") == 0) {
            space = std::make_unique<CosineSpace>(config->dimension);
        } else {
            space = std::make_unique<L2SpaceWrapper>(config->dimension);
        }
        
        auto index = std::make_unique<HierarchicalNSW<float>>(space.get());
        
        // Load the index
        index->loadIndex(std::string(path), space.get());
        
        // Store in maps
        indexes[index_name] = std::move(index);
        spaces[index_name] = std::move(space);
        
        return 0;
    } catch (...) {
        return -1;
    }
}

int hnswlib_set_ef(const char* name, size_t ef) {
    try {
        auto it = indexes.find(std::string(name));
        if (it == indexes.end() || !it->second) {
            return -1;
        }
        
        it->second->setEf(ef);
        return 0;
    } catch (...) {
        return -1;
    }
}

size_t hnswlib_get_current_count(const char* name) {
    auto it = indexes.find(std::string(name));
    if (it == indexes.end() || !it->second) {
        return 0;
    }
    
    return it->second->cur_element_count;
}

size_t hnswlib_get_max_elements(const char* name) {
    auto it = indexes.find(std::string(name));
    if (it == indexes.end() || !it->second) {
        return 0;
    }
    
    return it->second->max_elements_;
}

void hnswlib_clear_all(void) {
    indexes.clear();
    spaces.clear();
}

}