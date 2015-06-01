#include "scrf/scrf.h"
#include <istream>
#include <fstream>
#include "ebt/ebt.h"
#include "opt/opt.h"
#include "scrf/weiran.h"

namespace scrf {

    param_t load_param(std::istream& is)
    {
        param_t result;
        std::string line;

        result.class_param = ebt::json::json_parser<
            std::unordered_map<std::string, std::vector<real>>>().parse(is);
        std::getline(is, line);

        return result;
    }

    param_t load_param(std::string filename)
    {
        std::ifstream ifs { filename };
        return load_param(ifs);
    }

    void save_param(std::ostream& os, param_t const& param)
    {
        os << param.class_param << std::endl;
    }

    void save_param(std::string filename, param_t const& param)
    {
        std::ofstream ofs { filename };
        save_param(ofs, param);
    }

    param_t& operator-=(param_t& p1, param_t const& p2)
    {
        for (auto& p: p2.class_param) {
            auto& v = p1.class_param[p.first];

            v.resize(std::max(v.size(), p.second.size()));

            for (int i = 0; i < p.second.size(); ++i) {
                v[i] -= p.second[i];
            }
        }

        return p1;
    }

    param_t& operator+=(param_t& p1, param_t const& p2)
    {
        for (auto& p: p2.class_param) {
            auto& v = p1.class_param[p.first];

            v.resize(std::max(v.size(), p.second.size()));

            for (int i = 0; i < p.second.size(); ++i) {
                v[i] += p.second[i];
            }
        }

        return p1;
    }

    param_t& operator*=(param_t& p, real c)
    {
        if (c == 0) {
            p.class_param.clear();
        }

        for (auto& t: p.class_param) {
            for (int i = 0; i < t.second.size(); ++i) {
                t.second[i] *= c;
            }
        }

        return p;
    }

    real dot(param_t const& p1, param_t const& p2)
    {
        real sum = 0;

        for (auto& p: p2.class_param) {
            if (!ebt::in(p.first, p1.class_param)) {
                continue;
            }

            auto& v = p1.class_param.at(p.first);

            for (int i = 0; i < p.second.size(); ++i) {
                sum += v[i] * p.second[i];
            }
        }

        return sum;
    }

    void adagrad_update(param_t& param, param_t const& grad,
        param_t& accu_grad_sq, real step_size)
    {
        for (auto& p: grad.class_param) {
            if (!ebt::in(p.first, param.class_param)) {
                param.class_param[p.first].resize(p.second.size());
            }
            if (!ebt::in(p.first, accu_grad_sq.class_param)) {
                accu_grad_sq.class_param[p.first].resize(p.second.size());
            }
            opt::adagrad_update(param.class_param.at(p.first), p.second,
                accu_grad_sq.class_param.at(p.first), step_size);
        }
    }

    scrf_feature::~scrf_feature()
    {}

    void composite_feature::operator()(
        param_t& feat,
        fst::composed_fst<lattice::fst, lm::fst> const& fst,
        std::tuple<int, int> const& e) const
    {
        for (auto& f: features) {
            (*f)(feat, fst, e);
        }
    }
        
    scrf_weight::~scrf_weight()
    {}

    real composite_weight::operator()(fst::composed_fst<lattice::fst, lm::fst> const& fst,
        std::tuple<int, int> const& e) const
    {
        real sum = 0;

        for (auto& w: weights) {
            sum += (*w)(fst, e);
        }

        return sum;
    }

    linear_score::linear_score(param_t const& param, scrf_feature const& feat_func)
        : param(param), feat_func(feat_func)
    {}

    real linear_score::operator()(fst::composed_fst<lattice::fst, lm::fst> const& fst,
        std::tuple<int, int> const& e) const
    {
        param_t feat;

        feat_func(feat, fst, e);

        return dot(param, feat);
    }

    std::vector<scrf_t::vertex_type> scrf_t::vertices() const
    {
        return fst->vertices();
    }

    std::vector<scrf_t::edge_type> scrf_t::edges() const
    {
        return fst->edges();
    }

    scrf_t::vertex_type scrf_t::head(scrf_t::edge_type const& e) const
    {
        return fst->head(e);
    }

    scrf_t::vertex_type scrf_t::tail(scrf_t::edge_type const& e) const
    {
        return fst->tail(e);
    }

    std::vector<scrf_t::edge_type> scrf_t::in_edges(scrf_t::vertex_type const& v) const
    {
        return fst->in_edges(v);
    }

    std::vector<scrf_t::edge_type> scrf_t::out_edges(scrf_t::vertex_type const& v) const
    {
        return fst->out_edges(v);
    }

    real scrf_t::weight(scrf_t::edge_type const& e) const
    {
        return (*weight_func)(*fst, e);
    }

    std::string scrf_t::input(scrf_t::edge_type const& e) const
    {
        return fst->input(e);
    }

    std::string scrf_t::output(scrf_t::edge_type const& e) const
    {
        return fst->output(e);
    }

    std::vector<scrf_t::vertex_type> scrf_t::initials() const
    {
        return fst->initials();
    }

    std::vector<scrf_t::vertex_type> scrf_t::finals() const
    {
        return fst->finals();
    }

    namespace feature {

        bias::bias()
        {}

        void bias::operator()(
            param_t& feat,
            fst::composed_fst<lattice::fst, lm::fst> const& fst,
            std::tuple<int, int> const& e) const
        {
            feat.class_param["[label] " + fst.output(e)].push_back(1);
            feat.class_param["[label] shared"].push_back(1);
        }

        length_value::length_value(int max_seg)
            : max_seg(max_seg)
        {}

        void length_value::operator()(
            param_t& feat,
            fst::composed_fst<lattice::fst, lm::fst> const& fst,
            std::tuple<int, int> const& e) const
        {
            auto const& lat = *(fst.fst1);
            int tail = lat.tail(std::get<0>(e));
            int head = lat.head(std::get<0>(e));

            int tail_time = lat.data->vertices.at(tail).time;
            int head_time = lat.data->vertices.at(head).time;

            auto& v = feat.class_param["[lattice] " + fst.output(e)];

            v.push_back(head_time - tail_time);
            v.push_back(std::pow(head_time - tail_time, 2));
        }

        length_indicator::length_indicator(int max_seg)
            : max_seg(max_seg)
        {}

        void length_indicator::operator()(
            param_t& feat,
            fst::composed_fst<lattice::fst, lm::fst> const& fst,
            std::tuple<int, int> const& e) const
        {
            auto const& lat = *(fst.fst1);
            int tail = lat.tail(std::get<0>(e));
            int head = lat.head(std::get<0>(e));

            int tail_time = lat.data->vertices.at(tail).time;
            int head_time = lat.data->vertices.at(head).time;

            auto& v = feat.class_param["[lattice] " + fst.output(e)];
            int size = v.size();
            v.resize(size + max_seg + 1);

            if (fst.output(e) != "<s>" && fst.output(e) != "</s>" && fst.output(e) != "sil") {
                v.at(size + std::min(head_time - tail_time, max_seg)) = 1;
            }
        }

        frame_avg::frame_avg(std::vector<std::vector<real>> const& inputs,
            int start_dim, int end_dim)
            : inputs(inputs), start_dim(start_dim), end_dim(end_dim)
        {
            if (start_dim == -1) {
                start_dim = 0;
            }
            if (end_dim == -1) {
                end_dim = inputs.front().size() - 1;
            }
        }

        void frame_avg::operator()(
            param_t& feat,
            fst::composed_fst<lattice::fst, lm::fst> const& fst,
            std::tuple<int, int> const& e) const
        {
            if (ebt::in(std::get<0>(e), feat_cache)) {
                auto& u = feat_cache.at(std::get<0>(e));
                auto& v = feat.class_param["[lattice] " + fst.output(e)];
                v.insert(v.end(), u.begin(), u.end());
                return;
            }

            auto const& lat = *(fst.fst1);
            int tail = lat.tail(std::get<0>(e));
            int head = lat.head(std::get<0>(e));

            int tail_time = std::min<int>(inputs.size() - 1, lat.data->vertices.at(tail).time);
            int head_time = std::min<int>(inputs.size(), lat.data->vertices.at(head).time);

            std::vector<real> avg;
            avg.resize(inputs.front().size());

            if (tail_time < head_time) {
                for (int i = tail_time; i < head_time; ++i) {
                    auto const& v = inputs.at(i);

                    for (int j = start_dim; j < end_dim + 1; ++j) {
                        avg[j] += v.at(j);
                    }
                }

                for (int j = 0; j < avg.size(); ++j) {
                    avg[j] /= real(head_time - tail_time);
                }
            }

            auto& v = feat.class_param["[lattice] " + fst.output(e)];
            v.insert(v.end(), avg.begin(), avg.end());

            feat_cache[std::get<0>(e)] = std::move(avg);
        }

        frame_samples::frame_samples(std::vector<std::vector<real>> const& inputs,
            int samples, int start_dim, int end_dim)
            : inputs(inputs), samples(samples), start_dim(start_dim), end_dim(end_dim)
        {
            if (start_dim == -1) {
                start_dim = 0;
            }
            if (end_dim == -1) {
                end_dim = inputs.front().size() - 1;
            }
        }

        void frame_samples::operator()(
            param_t& feat,
            fst::composed_fst<lattice::fst, lm::fst> const& fst,
            std::tuple<int, int> const& e) const
        {
            auto const& lat = *(fst.fst1);
            int tail = lat.tail(std::get<0>(e));
            int head = lat.head(std::get<0>(e));

            int tail_time = lat.data->vertices.at(tail).time;
            int head_time = lat.data->vertices.at(head).time;

            real span = (head_time - tail_time) / samples;

            auto& v = feat.class_param["[lattice] " + fst.output(e)];
            for (int i = 0; i < samples; ++i) {
                auto& u = inputs.at(std::min<int>(std::floor(tail_time + (i + 0.5) * span), inputs.size() - 1));
                v.insert(v.end(), u.begin() + start_dim, u.end() + end_dim + 1);
            }
        }

        left_boundary::left_boundary(std::vector<std::vector<real>> const& inputs,
            int start_dim, int end_dim)
            : inputs(inputs), start_dim(start_dim), end_dim(end_dim)
        {
            if (start_dim == -1) {
                start_dim = 0;
            }
            if (end_dim == -1) {
                end_dim = inputs.front().size() - 1;
            }
        }

        void left_boundary::operator()(
            param_t& feat,
            fst::composed_fst<lattice::fst, lm::fst> const& fst,
            std::tuple<int, int> const& e) const
        {
            auto const& lm = *(fst.fst2);

            int lm_tail = lm.tail(std::get<1>(fst.tail(e)));

            std::string lex = lm.data->history.at(lm_tail) + "_" + fst.output(e);

            auto const& lat = *(fst.fst1);
            int tail = lat.tail(std::get<0>(e));
            int head = lat.head(std::get<0>(e));

            int tail_time = lat.data->vertices.at(tail).time;

            if (ebt::in(tail_time, feat_cache)) {
                auto& u = feat_cache.at(tail_time);
                auto& v = feat.class_param["[lattice] " + lex];
                v.insert(v.end(), u.begin(), u.end());
                return;
            }

            auto& v = feat.class_param["[lattice] " + lex];

            std::vector<real> f;
            for (int i = 0; i < 3; ++i) {
                auto& tail_u = inputs.at(std::min<int>(inputs.size() - 1, std::max<int>(tail_time - i, 0)));
                f.insert(f.end(), tail_u.begin() + start_dim, tail_u.end() + end_dim + 1);
            }
            v.insert(v.end(), f.begin(), f.end());

            feat_cache[tail_time] = std::move(f);
        }

        right_boundary::right_boundary(std::vector<std::vector<real>> const& inputs,
            int start_dim, int end_dim)
            : inputs(inputs)
        {
            if (start_dim == -1) {
                start_dim = 0;
            }
            if (end_dim == -1) {
                end_dim = inputs.front().size() - 1;
            }
        }

        void right_boundary::operator()(
            param_t& feat,
            fst::composed_fst<lattice::fst, lm::fst> const& fst,
            std::tuple<int, int> const& e) const
        {
            auto const& lm = *(fst.fst2);

            int lm_tail = lm.tail(std::get<1>(fst.tail(e)));

            std::string lex = lm.data->history.at(lm_tail) + "_" + fst.output(e);

            auto const& lat = *(fst.fst1);
            int tail = lat.tail(std::get<0>(e));
            int head = lat.head(std::get<0>(e));

            int head_time = lat.data->vertices.at(head).time;

            if (ebt::in(head_time, feat_cache)) {
                auto& u = feat_cache.at(head_time);
                auto& v = feat.class_param["[lattice] " + lex];
                v.insert(v.end(), u.begin(), u.end());
                return;
            }

            auto& v = feat.class_param["[lattice] " + lex];

            std::vector<real> f;
            for (int i = 0; i < 3; ++i) {
                auto& tail_u = inputs.at(std::min<int>(head_time + i, inputs.size() - 1));
                f.insert(f.end(), tail_u.begin() + start_dim, tail_u.end() + end_dim + 1);
            }
            v.insert(v.end(), f.begin(), f.end());

            feat_cache[head_time] = std::move(f);
        }

        void lm_score::operator()(
            param_t& feat,
            fst::composed_fst<lattice::fst, lm::fst> const& fst,
            std::tuple<int, int> const& e) const
        {
            feat.class_param["[lm] shared"].push_back(fst.fst2->weight(std::get<1>(e)));
        }

        void lattice_score::operator()(
            param_t& feat,
            fst::composed_fst<lattice::fst, lm::fst> const& fst,
            std::tuple<int, int> const& e) const
        {
            feat.class_param["[lattice] shared"].push_back(fst.fst1->weight(std::get<0>(e)));
        }

    }

    namespace score {

        linear_score::linear_score(param_t const& param,
                std::shared_ptr<scrf_feature> feat)
            : param(param), feat(feat)
        {}

        real linear_score::operator()(fst::composed_fst<lattice::fst, lm::fst> const& fst,
            std::tuple<int, int> const& e) const
        {
            param_t p;
            (*feat)(p, fst, e);
            real s = dot(param, p);

            return s;
        }

        label_score::label_score(param_t const& param,
                std::shared_ptr<scrf_feature> feat)
            : param(param), feat(feat)
        {}

        real label_score::operator()(fst::composed_fst<lattice::fst, lm::fst> const& fst,
            std::tuple<int, int> const& e) const
        {
            if (ebt::in(fst.output(e), cache)) {
                return cache[fst.output(e)];
            }

            param_t p;
            (*feat)(p, fst, e);
            real s = dot(param, p);

            cache[fst.output(e)] = s;

            return s;
        }

        lm_score::lm_score(param_t const& param,
                std::shared_ptr<scrf_feature> feat)
            : param(param), feat(feat)
        {}

        real lm_score::operator()(fst::composed_fst<lattice::fst, lm::fst> const& fst,
            std::tuple<int, int> const& e) const
        {
            if (ebt::in(std::get<1>(e), cache)) {
                return cache[std::get<1>(e)];
            }

            param_t p;
            (*feat)(p, fst, e);
            real s = dot(param, p);

            cache[std::get<1>(e)] = s;

            return s;
        }

        lattice_score::lattice_score(param_t const& param,
                std::shared_ptr<scrf_feature> feat)
            : param(param), feat(feat)
        {}

        real lattice_score::operator()(fst::composed_fst<lattice::fst, lm::fst> const& fst,
            std::tuple<int, int> const& e) const
        {
            if (ebt::in(std::make_tuple(std::get<0>(e), fst.output(e)), cache)) {
                return cache[std::make_tuple(std::get<0>(e), fst.output(e))];
            }

            param_t p;
            (*feat)(p, fst, e);
            real s = dot(param, p);

            cache[std::make_tuple(std::get<0>(e), fst.output(e))] = s;

            return s;
        }

    }

    std::vector<std::tuple<int, int>> topo_order(scrf_t const& scrf)
    {
        auto const& lat = *(scrf.fst->fst1);
        auto const& lm = *(scrf.fst->fst2);

        std::vector<std::tuple<int, int>> result;

        auto lm_vertices = lm.vertices();
        std::reverse(lm_vertices.begin(), lm_vertices.end());

        for (auto u: lat.vertices()) {
            for (auto v: lm_vertices) {
                result.push_back(std::make_tuple(u, v));
            }
        }

        return result;
    }

    fst::path<scrf_t> shortest_path(scrf_t const& s,
        std::vector<std::tuple<int, int>> const& order)
    {
        fst::one_best<scrf_t> best;

        for (auto v: s.initials()) {
            best.extra[v] = {std::make_tuple(-1, -1), 0};
        }

        best.merge(s, order);

        return best.best_path(s);
    }

    lattice::fst load_gold(std::istream& is)
    {
        std::string line;
        std::getline(is, line);
    
        lattice::fst_data result;

        int v = 0;
        result.initials.push_back(v);
        result.vertices.push_back(lattice::vertex_data{});
        result.vertices.at(v).time = 0;
    
        while (std::getline(is, line) && line != ".") {
            std::vector<std::string> parts = ebt::split(line);
    
            long tail_time = long(std::stol(parts.at(0)) / 1e5);
            long head_time = long(std::stol(parts.at(1)) / 1e5);
    
            if (tail_time == head_time) {
                continue;
            }

            int e = result.edges.size();
            int u = result.vertices.size();

            result.vertices.push_back(lattice::vertex_data{});
            result.vertices.at(u).time = head_time;

            if (u > int(result.in_edges.size()) - 1) {
                result.in_edges.resize(u + 1);
                result.in_edges_map.resize(u + 1);
            }

            result.in_edges[u].push_back(e);
            result.in_edges_map[u][parts.at(2)].push_back(e);

            if (v > int(result.out_edges.size()) - 1) {
                result.out_edges.resize(v + 1);
                result.out_edges_map.resize(v + 1);
            }

            result.out_edges[v].push_back(e);
            result.out_edges_map[v][parts.at(2)].push_back(e);

            result.edges.push_back(lattice::edge_data{});
            result.edges.at(e).tail = v;
            result.edges.at(e).head = u;
            result.edges.at(e).label = parts.at(2);

            v = u;
        }

        result.finals.push_back(v);

        lattice::fst f;
        f.data = std::make_shared<lattice::fst_data>(std::move(result));

        return f;
    }
    
    lattice::fst make_segmentation_lattice(int frames, int max_seg)
    {
        lattice::fst_data data;

        data.vertices.resize(frames + 1);
        for (int i = 0; i < frames + 1; ++i) {
            data.vertices.at(i).time = i;
        }

        data.in_edges.resize(frames + 1);
        data.out_edges.resize(frames + 1);
        data.in_edges_map.resize(frames + 1);
        data.out_edges_map.resize(frames + 1);

        for (int i = 0; i < frames + 1; ++i) {
            for (int j = 1; j <= max_seg; ++j) {
                int tail = i;
                int head = i + j;

                if (head > frames) {
                    continue;
                }

                data.edges.push_back(lattice::edge_data {"<label>", tail, head});
                int e = data.edges.size() - 1;

                data.in_edges.at(head).push_back(e);
                data.in_edges_map.at(head)["<label>"].push_back(e);
                data.out_edges.at(tail).push_back(e);
                data.in_edges_map.at(tail)["<label>"].push_back(e);
            }
        }

        data.initials.push_back(0);
        data.finals.push_back(frames);

        lattice::fst f;
        f.data = std::make_shared<lattice::fst_data>(std::move(data));

        return f;
    }

    std::shared_ptr<lm::fst> erase_input(std::shared_ptr<lm::fst> lm)
    {
        lm::fst result = *lm;
        result.data = std::make_shared<lm::fst_data>(*(lm->data));
        result.data->in_edges_map.clear();
        result.data->in_edges_map.resize(result.data->edges.size());
        result.data->out_edges_map.clear();
        result.data->out_edges_map.resize(result.data->edges.size());
        for (int e = 0; e < result.data->edges.size(); ++e) {
            auto& e_data = result.data->edges.at(e);
            e_data.input = "<label>";
            int tail = result.tail(e);
            int head = result.head(e);
            result.data->out_edges_map[tail]["<label>"].push_back(e);
            result.data->in_edges_map[head]["<label>"].push_back(e);
        }

        return std::make_shared<lm::fst>(result);
    }

    scrf_t make_gold_scrf(lattice::fst gold_lat,
        std::shared_ptr<lm::fst> lm)
    {
        gold_lat.data = std::make_shared<lattice::fst_data>(*(gold_lat.data));
        lattice::add_eps_loops(gold_lat);
        fst::composed_fst<lattice::fst, lm::fst> gold_lm_lat;
        gold_lm_lat.fst1 = std::make_shared<lattice::fst>(std::move(gold_lat));
        gold_lm_lat.fst2 = lm;

        scrf_t gold;
        gold.fst = std::make_shared<decltype(gold_lm_lat)>(gold_lm_lat);

        return gold;
    }

    real backoff_cost::operator()(fst::composed_fst<lattice::fst, lm::fst> const& fst,
        std::tuple<int, int> const& e) const
    {
        return fst.input(e) == "<eps>" ? -1 : 0;
    }

    overlap_cost::overlap_cost(fst::path<scrf_t> const& gold)
        : gold(gold)
    {}

    real overlap_cost::operator()(fst::composed_fst<lattice::fst, lm::fst> const& fst,
        std::tuple<int, int> const& e) const
    {
        int tail = std::get<0>(fst.tail(e));
        int head = std::get<0>(fst.head(e));

        int tail_time = fst.fst1->data->vertices.at(tail).time;
        int head_time = fst.fst1->data->vertices.at(head).time;

        if (tail == head) {
            return 0;
        }

        if (ebt::in(std::get<0>(e), edge_cache)) {
            
            int min_cost = std::numeric_limits<int>::max();

            for (auto& e_g: edge_cache.at(std::get<0>(e))) {
                int gold_tail = std::get<0>(gold.tail(e_g));
                int gold_head = std::get<0>(gold.head(e_g));

                int gold_tail_time = gold.data->base_fst->fst->fst1->data->vertices.at(gold_tail).time;
                int gold_head_time = gold.data->base_fst->fst->fst1->data->vertices.at(gold_head).time;

                int overlap = std::min(gold_head_time, head_time) - std::max(gold_tail_time, tail_time);
                int union_ = std::max(gold_head_time, head_time) - std::min(gold_tail_time, tail_time);
                int cost = (gold.output(e_g) == fst.output(e) ? union_ - overlap : union_);

                if (gold.output(e_g) == "<s>" && fst.output(e) == "<s>"
                        || gold.output(e_g) == "</s>" && fst.output(e) == "</s>"
                        || gold.output(e_g) == "sil" && fst.output(e) == "sil") {

                    cost = head_time - tail_time - overlap;
                }

                if (cost < min_cost) {
                    min_cost = cost;
                }
            }

            if (edge_cache.at(std::get<0>(e)).size() == 0) {
                return head_time - tail_time;
            }

            return min_cost;

        }

        int max_overlap = 0;
        std::vector<std::tuple<int, int>> max_overlap_edges;

        for (auto& e_g: gold.edges()) {
            int gold_tail = std::get<0>(gold.tail(e_g));
            int gold_head = std::get<0>(gold.head(e_g));

            int gold_tail_time = gold.data->base_fst->fst->fst1->data->vertices.at(gold_tail).time;
            int gold_head_time = gold.data->base_fst->fst->fst1->data->vertices.at(gold_head).time;

            int overlap = std::min(gold_head_time, head_time) - std::max(gold_tail_time, tail_time);

            if (overlap > max_overlap) {
                max_overlap = overlap;
                max_overlap_edges.clear();
                max_overlap_edges.push_back(e_g);
            } else if (overlap == max_overlap) {
                max_overlap_edges.push_back(e_g);
            }
        }

        int min_cost = std::numeric_limits<int>::max();

        for (auto& e_g: max_overlap_edges) {
            int gold_tail = std::get<0>(gold.tail(e_g));
            int gold_head = std::get<0>(gold.head(e_g));

            int gold_tail_time = gold.data->base_fst->fst->fst1->data->vertices.at(gold_tail).time;
            int gold_head_time = gold.data->base_fst->fst->fst1->data->vertices.at(gold_head).time;

            int overlap = std::min(gold_head_time, head_time) - std::max(gold_tail_time, tail_time);
            int union_ = std::max(gold_head_time, head_time) - std::min(gold_tail_time, tail_time);
            int cost = (gold.output(e_g) == fst.output(e) ? union_ - overlap : union_);

            if (gold.output(e_g) == "<s>" && fst.output(e) == "<s>"
                    || gold.output(e_g) == "</s>" && fst.output(e) == "</s>"
                    || gold.output(e_g) == "sil" && fst.output(e) == "sil") {

                cost = head_time - tail_time - overlap;
            }

            if (cost < min_cost) {
                min_cost = cost;
            }
        }

        edge_cache[std::get<0>(e)] = std::move(max_overlap_edges);

        if (max_overlap_edges.size() == 0) {
            return head_time - tail_time;
        }

        return min_cost;
    }

    neg_cost::neg_cost(std::shared_ptr<scrf_weight> cost)
        : cost(cost)
    {}

    real neg_cost::operator()(fst::composed_fst<lattice::fst, lm::fst> const& fst,
        std::tuple<int, int> const& e) const
    {
        return -(*cost)(fst, e);
    }

    scrf_t make_graph_scrf(int frames, std::shared_ptr<lm::fst> lm, int max_seg)
    {
        scrf_t result;

        lattice::fst segmentation = make_segmentation_lattice(frames, max_seg);
        lattice::add_eps_loops(segmentation);

        fst::composed_fst<lattice::fst, lm::fst> comp;
        comp.fst1 = std::make_shared<lattice::fst>(segmentation);
        comp.fst2 = lm;

        result.fst = std::make_shared<decltype(comp)>(comp);

        return result;
    }

    loss_func::~loss_func()
    {}

    hinge_loss::hinge_loss(fst::path<scrf_t> const& gold, scrf_t const& graph)
        : gold(gold), graph(graph)
    {
        graph_path = shortest_path(graph, graph.topo_order);

        if (graph_path.edges().size() == 0) {
            std::cout << "no cost aug path" << std::endl;
            exit(1);
        }
    }

    real hinge_loss::loss()
    {
        real gold_score = 0;

        std::cout << "gold: ";
        for (auto& e: gold.edges()) {
            std::cout << gold.output(e) << " ";
            gold_score += gold.weight(e);
        }
        std::cout << std::endl;

        std::cout << "gold score: " << gold_score << std::endl;

        real graph_score = 0;

        std::cout << "cost aug: ";
        for (auto& e: graph_path.edges()) {
            std::cout << graph.output(e) << " ";
            graph_score += graph_path.weight(e);
        }
        std::cout << std::endl;

        std::cout << "cost aug score: " << graph_score << std::endl; 

        return graph_score - gold_score;
    }

    param_t hinge_loss::param_grad()
    {
        param_t result;

        auto const& gold_feat = *(gold.data->base_fst->feature_func);

        for (auto& e: gold.edges()) {
            param_t p;
            gold_feat(p, *(gold.data->base_fst->fst), e);

            result -= p;
        }

        // std::cout << result.class_param.at("<s>") << std::endl;

        auto const& graph_feat = *(graph.feature_func);

        for (auto& e: graph_path.edges()) {
            param_t p;
            graph_feat(p, *(graph_path.data->base_fst->fst), e);

            result += p;
        }

        // std::cout << "grad of <s>: " << result.class_param.at("<s>") << std::endl;
        // std::cout << "grad of </s>: " << result.class_param.at("</s>") << std::endl;

        return result;
    }

    filtering_loss::filtering_loss(fst::path<scrf_t> const& gold,
        scrf_t const& graph, real alpha)
        : gold(gold), graph(graph), alpha(alpha)
    {
        graph_path = shortest_path(graph, graph.topo_order);

        auto order = graph.topo_order;

        for (auto v: graph.initials()) {
            forward.extra[v] = {std::make_tuple(-1, -1), 0};
            f_param[v] = param_t {};
        }
        forward.merge(graph, order);

        for (auto& v: order) {
            auto e = forward.extra.at(v).pi;
            if (e == std::make_tuple(-1, -1)) {
                continue;
            }
            param_t p;
            (*graph.feature_func)(p, *graph.fst, e);
            p += f_param.at(graph.tail(e));
            f_param[v] = std::move(p);
        }

        std::reverse(order.begin(), order.end());

        for (auto v: graph.finals()) {
            backward.extra[v] = {std::make_tuple(-1, -1), 0};
            b_param[v] = param_t {};
        }
        backward.merge(graph, order);

        for (auto& v: order) {
            auto e = backward.extra.at(v).pi;
            if (e == std::make_tuple(-1, -1)) {
                continue;
            }
            param_t p;
            (*graph.feature_func)(p, *graph.fst, e);
            p += b_param.at(graph.head(e));
            b_param[v] = std::move(p);
        }

        auto fb_alpha = [&](std::tuple<int, int> const& v) {
            return forward.extra[v].value;
        };

        auto fb_beta = [&](std::tuple<int, int> const& v) {
            return backward.extra[v].value;
        };

        real inf = std::numeric_limits<real>::infinity();

        auto edges = graph.edges();

        real sum = 0;
        real max = -inf;

        for (auto& e: edges) {
            auto tail = graph.tail(e);
            auto head = graph.head(e);

            real s = fb_alpha(tail) + graph.weight(e) + fb_beta(head);

            if (s > max) {
                max = s;
            }

            if (s != -inf) {
                sum += s;
            }
        }

        threshold = alpha * max + (1 - alpha) * sum / edges.size();

        real f_max = -inf;

        for (auto v: graph.finals()) {
            if (forward.extra.at(v).value > f_max) {
                f_max = forward.extra.at(v).value;
            }
        }

        real b_max = -inf;

        for (auto v: graph.initials()) {
            if (backward.extra.at(v).value > b_max) {
                b_max = backward.extra.at(v).value;
            }
        }

        if (!(std::fabs(f_max - b_max) / std::fabs(b_max) < 0.001
                && std::fabs(max - b_max) / std::fabs(b_max) < 0.001)) {
            std::cout << "forward: " << f_max << " backward: " << b_max << " max: " << max << std::endl;
            exit(1);
        }
    }

    real filtering_loss::loss()
    {
        real gold_score = 0;

        for (auto e: gold.edges()) {
            gold_score += gold.weight(e);
        }

        return std::max<real>(0.0, 1 + threshold - gold_score);
    }

    param_t filtering_loss::param_grad()
    {
        param_t result;

        auto edges = graph.edges();

        for (auto e: edges) {
            param_t p;
            (*graph.feature_func)(p, *graph.fst, e);
            p += f_param.at(graph.tail(e));
            p += b_param.at(graph.head(e));

            p *= (1 - alpha) / edges.size();

            result += p;
        }

        for (auto e: graph_path.edges()) {
            param_t p;
            (*graph.feature_func)(p, *(graph_path.data->base_fst->fst), e);

            p *= alpha;

            result += p;
        }

        for (auto e: gold.edges()) {
            param_t p;
            (*gold.data->base_fst->feature_func)(p, *(gold.data->base_fst->fst), e);
            result -= p;
        }

        return result;
    }

    composite_feature make_feature(
        std::vector<std::string> features,
        std::vector<std::vector<real>> const& inputs, int max_seg)
    {
        return make_feature(features, inputs, max_seg,
            std::vector<real>(), std::vector<real>(), weiran::nn_t());
    }

    composite_feature make_feature(
        std::vector<std::string> features,
        std::vector<std::vector<real>> const& inputs, int max_seg,
        std::vector<real> const& cm_mean,
        std::vector<real> const& cm_stddev,
        weiran::nn_t const& nn)
    {
        composite_feature result;
    
        for (auto& v: features) {
            if (ebt::startswith(v, "frame-avg")) {
                std::vector<std::string> parts = ebt::split(v, ":");
                if (parts.size() == 2) {
                    std::vector<std::string> indices = ebt::split(parts.front(), "-");
                    result.features.push_back(std::make_shared<feature::frame_avg>(
                        feature::frame_avg { inputs, std::stoi(indices.at(0)), std::stoi(indices.at(1)) }));
                } else {
                    result.features.push_back(std::make_shared<feature::frame_avg>(
                        feature::frame_avg { inputs }));
                }
            } else if (ebt::startswith(v, "frame-samples")) {
                std::vector<std::string> parts = ebt::split(v, ":");
                if (parts.size() == 2) {
                    std::vector<std::string> indices = ebt::split(parts.front(), "-");
                    result.features.push_back(std::make_shared<feature::frame_samples>(
                        feature::frame_samples { inputs, 3, std::stoi(indices.at(0)), std::stoi(indices.at(1)) }));
                } else {
                    result.features.push_back(std::make_shared<feature::frame_samples>(
                        feature::frame_samples { inputs, 3 }));
                }
            } else if (ebt::startswith(v, "left-boundary")) {
                std::vector<std::string> parts = ebt::split(v, ":");
                if (parts.size() == 2) {
                    std::vector<std::string> indices = ebt::split(parts.front(), "-");
                    result.features.push_back(std::make_shared<feature::left_boundary>(
                        feature::left_boundary { inputs, std::stoi(indices.at(0)), std::stoi(indices.at(1)) }));
                } else {
                    result.features.push_back(std::make_shared<feature::left_boundary>(
                        feature::left_boundary { inputs }));
                }
            } else if (ebt::startswith(v, "right-boundary")) {
                std::vector<std::string> parts = ebt::split(v, ":");
                if (parts.size() == 2) {
                    std::vector<std::string> indices = ebt::split(parts.front(), "-");
                    result.features.push_back(std::make_shared<feature::right_boundary>(
                        feature::right_boundary { inputs, std::stoi(indices.at(0)), std::stoi(indices.at(1)) }));
                } else {
                    result.features.push_back(std::make_shared<feature::right_boundary>(
                        feature::right_boundary { inputs }));
                }
            } else if (ebt::startswith(v, "length-indicator")) {
                result.features.push_back(std::make_shared<feature::length_indicator>(
                    feature::length_indicator { max_seg }));
            } else if (ebt::startswith(v, "length-value")) {
                result.features.push_back(std::make_shared<feature::length_value>(
                    feature::length_value { max_seg }));
            } else if (ebt::startswith(v, "bias")) {
                result.features.push_back(std::make_shared<feature::bias>(feature::bias{}));
            } else if (ebt::startswith(v, "lm-score")) {
                result.features.push_back(std::make_shared<feature::lm_score>(feature::lm_score{}));
            } else if (ebt::startswith(v, "lattice-score")) {
                result.features.push_back(std::make_shared<feature::lattice_score>(feature::lattice_score{}));
            } else if (ebt::startswith(v, "weiran")) {
                std::vector<std::string> parts = ebt::split(v, ":");
                if (parts.size() == 2) {
                    std::vector<std::string> indices = ebt::split(parts.front(), "-");
                    result.features.push_back(std::make_shared<weiran::weiran_feature>(
                        weiran::weiran_feature { inputs, cm_mean, cm_stddev, nn,
                        std::stoi(indices.at(0)), std::stoi(indices.at(1)) }));
                } else {
                    result.features.push_back(std::make_shared<weiran::weiran_feature>(
                        weiran::weiran_feature { inputs, cm_mean, cm_stddev, nn }));
                }
            } else {
                std::cout << "unknown feature type " << v << std::endl;
                exit(1);
            }
        }
    
        return result;
    }

    composite_weight make_weight(
        param_t const& param,
        std::vector<std::string> features,
        composite_feature const& feat)
    {
        composite_weight result;

        composite_feature label_feat;
        composite_feature lm_feat;
        composite_feature lattice_feat;
        composite_feature rest_feat;

        for (int i = 0; i < features.size(); ++i) {
            auto& v = features[i];

            if (ebt::startswith(v, "frame-avg")) {
                lattice_feat.features.push_back(feat.features[i]);
            } else if (ebt::startswith(v, "frame-samples")) {
                lattice_feat.features.push_back(feat.features[i]);
            } else if (ebt::startswith(v, "left-boundary")) {
                rest_feat.features.push_back(feat.features[i]);
            } else if (ebt::startswith(v, "right-boundary")) {
                rest_feat.features.push_back(feat.features[i]);
            } else if (ebt::startswith(v, "length-indicator")) {
                lattice_feat.features.push_back(feat.features[i]);
            } else if (ebt::startswith(v, "length-value")) {
                lattice_feat.features.push_back(feat.features[i]);
            } else if (ebt::startswith(v, "bias")) {
                label_feat.features.push_back(feat.features[i]);
            } else if (ebt::startswith(v, "lm-score")) {
                lm_feat.features.push_back(feat.features[i]);
            } else if (ebt::startswith(v, "lattice-score")) {
                lattice_feat.features.push_back(feat.features[i]);
            } else if (ebt::startswith(v, "weiran")) {
                lattice_feat.features.push_back(feat.features[i]);
            } else {
                std::cout << "unknown feature type " << v << std::endl;
                exit(1);
            }
        }
    
        score::label_score label_score { param, std::make_shared<composite_feature>(label_feat) };
        score::lm_score lm_score { param, std::make_shared<composite_feature>(lm_feat) };
        score::lattice_score lattice_score { param, std::make_shared<composite_feature>(lattice_feat) };
        score::linear_score rest_score { param, std::make_shared<composite_feature>(rest_feat) };

        result.weights.push_back(std::make_shared<score::label_score>(label_score));
        result.weights.push_back(std::make_shared<score::lm_score>(lm_score));
        result.weights.push_back(std::make_shared<score::lattice_score>(lattice_score));
        result.weights.push_back(std::make_shared<score::linear_score>(rest_score));

        return result;
    }

#if 0
    lattice::fst make_lattice(
        std::vector<std::vector<real>> acoustics,
        std::unordered_set<std::string> phone_set,
        int seg_size)
    {
        lattice::fst_data result;

        result.initial = 0;
        result.final = acoustics.size();

        for (int i = 0; i <= acoustics.size(); ++i) {
            lattice::vertex_data v_data;
            v_data.time = i;
            result.vertices.push_back(v_data);
        }

        for (int i = 0; i <= acoustics.size(); ++i) {
            for (auto& p: phone_set) {
                if (p == "<eps>") {
                    continue;
                }

                for (int j = 1; j <= seg_size && i + j <= acoustics.size(); ++j) {
                    int tail = i;
                    int head = i + j;

                    result.edges.push_back(lattice::edge_data { .label = p,
                        .tail = tail, .head = head });

                    if (std::max(tail, head) >= int(result.in_edges.size()) - 1) {
                        result.in_edges.resize(std::max(tail, head) + 1);
                        result.out_edges.resize(std::max(tail, head) + 1);
                        result.in_edges_map.resize(std::max(tail, head) + 1);
                        result.out_edges_map.resize(std::max(tail, head) + 1);
                    }

                    result.in_edges[head].push_back(int(result.edges.size()) - 1);
                    result.out_edges[tail].push_back(int(result.edges.size()) - 1);

                    result.out_edges_map.at(tail)[p].push_back(result.edges.size() - 1);
                    result.in_edges_map.at(head)[p].push_back(result.edges.size() - 1);
                }
            }
        }

        lattice::fst f;
        f.data = std::make_shared<lattice::fst_data>(std::move(result));
    
        return f;
    }

    void forward_backward_alg::forward_score(scrf const& s)
    {
        alpha.reserve(s.topo_order.size());
        real inf = std::numeric_limits<real>::infinity();

        alpha[s.topo_order.front()] = 0;

        for (int i = 1; i < s.topo_order.size(); ++i) {
            auto const& u = s.topo_order.at(i);

            real value = -std::numeric_limits<real>::infinity();
            for (auto& e: s.in_edges(u)) {
                std::tuple<int, int> v = s.tail(e);
                if (alpha.at(v) != -inf) {
                    value = ebt::log_add(value, alpha.at(v) + s.weight(e));
                }
            }
            alpha[u] = value;
        }
    }

    void forward_backward_alg::backward_score(scrf const& s)
    {
        beta.reserve(s.topo_order.size());
        real inf = std::numeric_limits<real>::infinity();

        beta[s.topo_order.back()] = 0;
        for (int i = s.topo_order.size() - 2; i >= 0; --i) {
            auto const& u = s.topo_order.at(i);

            real value = -std::numeric_limits<real>::infinity();
            for (auto& e: s.out_edges(u)) {
                std::tuple<int, int> v = s.head(e);
                if (beta.at(v) != -inf) {
                    value = ebt::log_add(value, beta.at(v) + s.weight(e));
                }
            }
            beta[u] = value;
        }
    }

    std::unordered_map<std::string, std::vector<real>>
    forward_backward_alg::feature_expectation(scrf const& s)
    {
        /*
        real logZ = alpha.at(s.final());
        auto const& feat_func = *(s.feature_func);
        real inf = std::numeric_limits<real>::infinity();

        std::unordered_map<std::string, std::vector<real>> result;

        for (auto& p2: fst2_edge_index) {
            for (auto& e1: s.fst->fst1->edges()) {
                auto const& feat = feat_func();

                real prob_sum = -inf;

                for (auto& e2: p2.second) {
                    auto e = std::make_tuple(e1, e2);

                    auto tail = s.tail(e);
                    auto head = s.head(e);

                    if (alpha.at(tail) == -inf || beta.at(head) == -inf) {
                        continue;
                    }

                    real prob = alpha.at(tail) + beta.at(head) + s.weight(e) - logZ;
                    prob_sum = ebt::log_add(prob, prob_sum);
                }

                prob_sum = std::exp(prob_sum);

                for (auto& p: feat) {
                    result[p.first].resize(p.second.size());
                    auto& v = result.at(p.first);
                    for (int i = 0; i < p.second.size(); ++i) {
                        v.at(i) += p.second.at(i) * prob_sum;
                    }
                }
            }
        }

        return result;
        */
    }

    log_loss::log_loss(fst::path<scrf> const& gold, scrf const& lat)
        : gold(gold), lat(lat)
    {
        fb.forward_score(lat);
        fb.backward_score(lat);
        std::cout << fb.alpha.at(lat.final()) << " " << fb.beta.at(lat.initial()) << std::endl;
    }

    real log_loss::loss()
    {
        real sum = 0;
        for (auto& e: gold.edges()) {
            sum += gold.weight(e);
        }

        return -sum + fb.alpha.at(lat.final());
    }

    std::unordered_map<std::string, std::vector<real>> const& log_loss::model_grad()
    {
        /*
        result.clear();

        auto const& gold_feat = *(gold.data->base_fst->feature_func);

        for (auto& e: gold.edges()) {
            for (auto& p: gold_feat(e)) {
                result[p.first].resize(p.second.size());
                auto& v = result.at(p.first);
                for (int i = 0; i < p.second.size(); ++i) {
                    v.at(i) -= p.second.at(i);
                }
            }
        }

        auto const& graph_feat = fb.feature_expectation(lat);

        for (auto& p: graph_feat) {
            result[p.first].resize(p.second.size());
            auto& v = result.at(p.first);
            for (int i = 0; i < p.second.size(); ++i) {
                v.at(i) += p.second.at(i);
            }
        }

        return result;
        */
    }

    frame_feature::frame_feature(std::vector<std::vector<real>> const& inputs)
        : inputs(inputs)
    {
        cache.reserve(4000);
    }

    std::unordered_map<std::string, std::vector<real>> const&
    frame_feature::operator()(std::string const& y, int start_time, int end_time) const
    {
        auto k = std::make_tuple(start_time, end_time);

        if (ebt::in(k, cache)) {
            auto& m = cache.at(k);

            std::string ell;

            for (auto& p: m) {
                if (p.first != "shared") {
                    ell = p.first;
                    break;
                }
            }

            std::vector<real> vec = std::move(m.at(ell));

            m[y] = std::move(vec);

            return m;
        }

        std::unordered_map<std::string, std::vector<real>> result;

        real span = (end_time - start_time) / 10;

        auto& vec = result[y];

        for (int i = 0; i < 10; ++i) {
            int frame = std::floor(start_time + (i + 0.5) * span);

            vec.insert(vec.end(), inputs.at(frame).begin(), inputs.at(frame).end());
        }

        cache[k] = std::move(result);

        return cache.at(k);
    }

    frame_score::frame_score(frame_feature const& feat, param_t const& model)
        : feat(feat), model(model)
    {
        cache.reserve(3000000);
    }

    real frame_score::operator()(std::string const& y, int start_time, int end_time) const
    {
        auto k = std::make_tuple(y, start_time, end_time);

        if (ebt::in(k, cache)) {
            return cache.at(k);
        }

        real sum = 0;
        for (auto& p: feat(y, start_time, end_time)) {
            auto const& w = model.weights.at(p.first);

            for (int i = 0; i < p.second.size(); ++i) {
                sum += w.at(i) * p.second.at(i);
            }
        }

        cache[k] = sum;

        return sum;
    }

    linear_score::linear_score(fst::composed_fst<lattice::fst, lm::fst> const& fst,
        frame_score const& f_score)
        : fst(fst), f_score(f_score)
    {
    }

    real linear_score::operator()(std::tuple<int, int> const& e) const
    {
        auto const& lat = *(fst.fst1);

        int tail = lat.tail(std::get<0>(e));
        int head = lat.head(std::get<0>(e));

        int tail_time = lat.data->vertices.at(tail).time;
        int head_time = lat.data->vertices.at(head).time;

        return f_score(fst.output(e), tail_time, head_time);
    }
#endif

}
