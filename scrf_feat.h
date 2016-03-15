#ifndef FEAT_H
#define FEAT_H

#include "scrf/util.h"
#include "scrf/scrf.h"
#include "scrf/segfeat.h"

namespace scrf {

    struct composite_feature
        : public scrf_feature {

        std::vector<std::shared_ptr<scrf_feature>> features;

        composite_feature();

        virtual void operator()(
            feat_t& feat,
            fst::composed_fst<lattice::fst, lm::fst> const& fst,
            std::tuple<int, int> const& e) const override;
    };

    std::vector<double>& lexicalize(int order, feat_t& feat,
        fst::composed_fst<lattice::fst, lm::fst> const& fst,
        std::tuple<int, int> const& e);

    struct segment_feature
        : public scrf_feature {

        segment_feature(
            int order,
            std::shared_ptr<segfeat::feature> raw_feat_func,
            std::vector<std::vector<real>> const& frames);

        int order;
        std::shared_ptr<segfeat::feature> feat_func;
        std::vector<std::vector<real>> const& frames;

        virtual void operator()(
            feat_t& feat,
            fst::composed_fst<lattice::fst, lm::fst> const& fst,
            std::tuple<int, int> const& e) const override;
    };

    namespace feature {

        struct lm_score
            : public scrf_feature {

            virtual void operator()(
                feat_t& feat,
                fst::composed_fst<lattice::fst, lm::fst> const& fst,
                std::tuple<int, int> const& e) const override;
        };

        struct lattice_score
            : public scrf_feature {

            virtual void operator()(
                feat_t& feat,
                fst::composed_fst<lattice::fst, lm::fst> const& fst,
                std::tuple<int, int> const& e) const override;

        };

        struct external_feature
            : public scrf_feature {

            int order;
            std::vector<int> dims;

            external_feature(int order, std::vector<int> dims);

            virtual void operator()(
                feat_t& feat,
                fst::composed_fst<lattice::fst, lm::fst> const& fst,
                std::tuple<int, int> const& e) const override;

        };

        struct frame_feature
            : public scrf::scrf_feature {
        
            std::vector<std::vector<double>> const& frames;
            std::unordered_map<std::string, int> const& phone_id;
        
            frame_feature(std::vector<std::vector<double>> const& frames,
                std::unordered_map<std::string, int> const& phone_set);
        
            virtual void operator()(
                feat_t& feat,
                fst::composed_fst<lattice::fst, lm::fst> const& fst,
                std::tuple<int, int> const& e) const;
        
        };

    }

    namespace first_order {

        struct composite_feature
            : public scrf_feature {

            std::vector<std::shared_ptr<scrf_feature>> features;

            composite_feature();

            virtual void operator()(
                param_t& feat, ilat::fst const& fst, int e) const override;

        };

        la::vector<double>& lexicalize(feat_dim_alloc const& alloc,
            int order, param_t& feat, ilat::fst const& fst, int e);

        struct segment_feature
            : public scrf_feature {

            segment_feature(
                feat_dim_alloc& alloc,
                int order,
                std::shared_ptr<segfeat::la::feature> raw_feat_func,
                std::vector<std::vector<real>> const& frames);

            feat_dim_alloc& alloc;
            int dim;
            int order;
            std::shared_ptr<segfeat::la::feature> feat_func;
            std::vector<std::vector<real>> const& frames;

            virtual void operator()(
                param_t& feat, ilat::fst const& fst, int e) const override;
        };

        namespace feature {

            struct lattice_score
                : public scrf_feature {

                lattice_score(feat_dim_alloc& alloc);

                feat_dim_alloc& alloc;
                int dim;

                virtual void operator()(
                    param_t& feat, ilat::fst const& fst, int e) const override;

            };

            struct external_feature
                : public scrf_feature {

                external_feature(feat_dim_alloc& alloc,
                    int order, std::vector<int> dims);

                feat_dim_alloc& alloc;
                int dim;
                int order;
                std::vector<int> dims;

                virtual void operator()(
                    param_t& feat, ilat::fst const& fst, int e) const override;

            };

            struct frame_feature
                : public scrf_feature {
            
                frame_feature(feat_dim_alloc& alloc,
                    std::vector<std::vector<double>> const& frames);
            
                feat_dim_alloc& alloc;
                int dim;
                std::vector<std::vector<double>> const& frames;
            
                virtual void operator()(
                    param_t& feat, ilat::fst const& fst, int e) const;
            
            };

        }

    }

}

#endif
