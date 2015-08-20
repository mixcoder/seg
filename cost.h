#ifndef COST_H
#define COST_H

#include "scrf/scrf.h"
#include "speech/speech.h"

namespace scrf {

    namespace cost {

        double overlap_cost(std::vector<speech::segment> const& gold_segs,
            speech::segment const& e);

        struct cost : public scrf_weight {

            cost(fst::path<scrf::scrf_t> const& gold_fst);

            std::vector<speech::segment> gold_segs;

            virtual real operator()(fst::composed_fst<lattice::fst, lm::fst> const& fst,
                std::tuple<int, int> const& e) const override;

        };

    }

}

namespace scrf {

    struct backoff_cost
        : public scrf_weight {

        virtual real operator()(fst::composed_fst<lattice::fst, lm::fst> const& fst,
            std::tuple<int, int> const& e) const;
    };

    struct overlap_cost
        : public scrf_weight {

        fst::path<scrf_t> const& gold;

        mutable std::unordered_map<int, std::vector<std::tuple<int, int>>> edge_cache;

        overlap_cost(fst::path<scrf_t> const& gold);

        virtual real operator()(fst::composed_fst<lattice::fst, lm::fst> const& fst,
            std::tuple<int, int> const& e) const;

    };

    struct neg_cost
        : public scrf_weight {

        std::shared_ptr<scrf_weight> cost;

        neg_cost(std::shared_ptr<scrf_weight> cost);

        virtual real operator()(fst::composed_fst<lattice::fst, lm::fst> const& fst,
            std::tuple<int, int> const& e) const;
    };

}

#endif
