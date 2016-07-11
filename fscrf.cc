#include "scrf/fscrf.h"
#include "scrf/scrf_weight.h"
#include "scrf/util.h"
#include "scrf/scrf.h"
#include <fstream>

namespace fscrf {

    std::shared_ptr<ilat::fst> make_graph(int frames,
        std::unordered_map<std::string, int> const& label_id,
        std::vector<std::string> const& id_label,
        int min_seg_len, int max_seg_len, int stride)
    {
        assert(stride >= 1);
        assert(min_seg_len >= 1);
        assert(max_seg_len >= min_seg_len);

        ilat::fst_data data;

        data.symbol_id = std::make_shared<std::unordered_map<std::string, int>>(label_id);
        data.id_symbol = std::make_shared<std::vector<std::string>>(id_label);

        int i = 0;
        int v = -1;
        for (i = 0; i < frames + 1; i += stride) {
            ++v;
            ilat::add_vertex(data, v, ilat::vertex_data { i });
        }

        if (frames % stride != 0) {
            ++v;
            ilat::add_vertex(data, v, ilat::vertex_data { frames });
        }

        data.initials.push_back(0);
        data.finals.push_back(v);

        for (int u = 0; u < data.vertices.size(); ++u) {
            for (int v = u + 1; v < data.vertices.size(); ++v) {
                int duration = data.vertices[v].time - data.vertices[u].time;

                if (duration < min_seg_len) {
                    continue;
                }

                if (duration > max_seg_len) {
                    break;
                }

                for (auto& p: label_id) {
                    if (p.second == 0) {
                        continue;
                    }

                    ilat::add_edge(data, data.edges.size(),
                        ilat::edge_data { u, v, 0, p.second, p.second });
                }
            }
        }

        ilat::fst result;
        result.data = std::make_shared<ilat::fst_data>(std::move(data));

        return std::make_shared<ilat::fst>(result);
    }

    std::shared_ptr<tensor_tree::vertex> make_tensor_tree(
        std::vector<std::string> const& features)
    {
        std::unordered_set<std::string> feature_keys { features.begin(), features.end() };

        tensor_tree::vertex root { tensor_tree::tensor_t::nil };

        if (ebt::in(std::string("frame-avg"), feature_keys)) {
            root.children.push_back(tensor_tree::make_matrix("frame avg"));
        }

        if (ebt::in(std::string("frame-samples"), feature_keys)) {
            root.children.push_back(tensor_tree::make_matrix("frame samples"));
            root.children.push_back(tensor_tree::make_matrix("frame samples"));
            root.children.push_back(tensor_tree::make_matrix("frame samples"));
        }

        if (ebt::in(std::string("left-boundary"), feature_keys)) {
            root.children.push_back(tensor_tree::make_matrix("left boundary"));
            root.children.push_back(tensor_tree::make_matrix("left boundary"));
            root.children.push_back(tensor_tree::make_matrix("left boundary"));
        }

        if (ebt::in(std::string("right-boundary"), feature_keys)) {
            root.children.push_back(tensor_tree::make_matrix("right boundary"));
            root.children.push_back(tensor_tree::make_matrix("right boundary"));
            root.children.push_back(tensor_tree::make_matrix("right boundary"));
        }

        if (ebt::in(std::string("length-indicator"), feature_keys)) {
            root.children.push_back(tensor_tree::make_matrix("length"));
        }

        if (ebt::in(std::string("log-length"), feature_keys)) {
            root.children.push_back(tensor_tree::make_matrix("log length"));
        }

        if (ebt::in(std::string("bias"), feature_keys)) {
            root.children.push_back(tensor_tree::make_vector("bias"));
        }

        return std::make_shared<tensor_tree::vertex>(root);
    }

    std::shared_ptr<scrf::composite_weight<ilat::fst>> make_weights(
        std::vector<std::string> const& features,
        std::shared_ptr<tensor_tree::vertex> var_tree,
        std::shared_ptr<autodiff::op_t> frame_mat)
    {
        std::unordered_set<std::string> feature_keys { features.begin(), features.end() };

        scrf::composite_weight<ilat::fst> weight_func;

        int feat_idx = 0;

        if (ebt::in(std::string("frame-avg"), feature_keys)) {
            weight_func.weights.push_back(std::make_shared<fscrf::frame_avg_score>(
                fscrf::frame_avg_score(tensor_tree::get_var(var_tree->children[feat_idx]), frame_mat)));

            ++feat_idx;
        }

        if (ebt::in(std::string("frame-samples"), feature_keys)) {
            weight_func.weights.push_back(std::make_shared<fscrf::frame_samples_score>(
                fscrf::frame_samples_score(tensor_tree::get_var(var_tree->children[feat_idx]), frame_mat, 1.0 / 6)));
            weight_func.weights.push_back(std::make_shared<fscrf::frame_samples_score>(
                fscrf::frame_samples_score(tensor_tree::get_var(var_tree->children[feat_idx + 1]), frame_mat, 1.0 / 2)));
            weight_func.weights.push_back(std::make_shared<fscrf::frame_samples_score>(
                fscrf::frame_samples_score(tensor_tree::get_var(var_tree->children[feat_idx + 2]), frame_mat, 5.0 / 6)));

            feat_idx += 3;
        }

        if (ebt::in(std::string("left-boundary"), feature_keys)) {
            weight_func.weights.push_back(std::make_shared<fscrf::left_boundary_score>(
                fscrf::left_boundary_score(tensor_tree::get_var(var_tree->children[feat_idx]), frame_mat, -1)));
            weight_func.weights.push_back(std::make_shared<fscrf::left_boundary_score>(
                fscrf::left_boundary_score(tensor_tree::get_var(var_tree->children[feat_idx + 1]), frame_mat, -2)));
            weight_func.weights.push_back(std::make_shared<fscrf::left_boundary_score>(
                fscrf::left_boundary_score(tensor_tree::get_var(var_tree->children[feat_idx + 2]), frame_mat, -3)));

            feat_idx += 3;
        }

        if (ebt::in(std::string("left-boundary"), feature_keys)) {
            weight_func.weights.push_back(std::make_shared<fscrf::right_boundary_score>(
                fscrf::right_boundary_score(tensor_tree::get_var(var_tree->children[feat_idx]), frame_mat, 1)));
            weight_func.weights.push_back(std::make_shared<fscrf::right_boundary_score>(
                fscrf::right_boundary_score(tensor_tree::get_var(var_tree->children[feat_idx + 1]), frame_mat, 2)));
            weight_func.weights.push_back(std::make_shared<fscrf::right_boundary_score>(
                fscrf::right_boundary_score(tensor_tree::get_var(var_tree->children[feat_idx + 2]), frame_mat, 3)));

            feat_idx += 3;
        }

        if (ebt::in(std::string("length-indicator"), feature_keys)) {
            weight_func.weights.push_back(std::make_shared<fscrf::length_score>(
                fscrf::length_score { tensor_tree::get_var(var_tree->children[feat_idx]) }));

            ++feat_idx;
        }

        if (ebt::in(std::string("log-length"), feature_keys)) {
            weight_func.weights.push_back(std::make_shared<fscrf::log_length_score>(
                fscrf::log_length_score { tensor_tree::get_var(var_tree->children[feat_idx]) }));

            ++feat_idx;
        }

        if (ebt::in(std::string("bias"), feature_keys)) {
            weight_func.weights.push_back(std::make_shared<fscrf::bias_score>(
                fscrf::bias_score { tensor_tree::get_var(var_tree->children[feat_idx]) }));

            ++feat_idx;
        }

        return std::make_shared<scrf::composite_weight<ilat::fst>>(weight_func);
    }

    frame_avg_score::frame_avg_score(std::shared_ptr<autodiff::op_t> param,
            std::shared_ptr<autodiff::op_t> frames)
        : param(param), frames(frames)
    {
        score = autodiff::mmul(param, frames);
        autodiff::eval_vertex(score, autodiff::eval_funcs);
    }

    double frame_avg_score::operator()(ilat::fst const& f,
        int e) const
    {
        auto& m = autodiff::get_output<la::matrix<double>>(score);

        double sum = 0;

        int ell = f.output(e) - 1;
        int tail_time = f.time(f.tail(e));
        int head_time = f.time(f.head(e));

        for (int t = tail_time; t < head_time; ++t) {
            sum += m(ell, t);
        }

        return sum / (head_time - tail_time);
    }

    void frame_avg_score::accumulate_grad(double g, ilat::fst const& f,
        int e) const
    {
        auto& m = autodiff::get_output<la::matrix<double>>(score);

        if (score->grad == nullptr) {
            la::matrix<double> m_grad;
            m_grad.resize(m.rows(), m.cols());
            score->grad = std::make_shared<la::matrix<double>>(std::move(m_grad));
        }

        auto& m_grad = autodiff::get_grad<la::matrix<double>>(score);

        int ell = f.output(e) - 1;
        int tail_time = f.time(f.tail(e));
        int head_time = f.time(f.head(e));

        for (int t = tail_time; t < head_time; ++t) {
            m_grad(ell, t) += g / (head_time - tail_time);
        }
    }

    void frame_avg_score::grad() const
    {
        autodiff::eval_vertex(score, autodiff::grad_funcs);
    }

    frame_samples_score::frame_samples_score(std::shared_ptr<autodiff::op_t> param,
            std::shared_ptr<autodiff::op_t> frames, double scale)
        : param(param), frames(frames), scale(scale)
    {
        score = autodiff::mmul(param, frames);
        autodiff::eval_vertex(score, autodiff::eval_funcs);
    }

    double frame_samples_score::operator()(ilat::fst const& f,
        int e) const
    {
        auto& m = autodiff::get_output<la::matrix<double>>(score);

        int ell = f.output(e) - 1;
        int tail_time = f.time(f.tail(e));
        int head_time = f.time(f.head(e));

        int dur = head_time - tail_time;

        return m(ell, tail_time + dur * scale);
    }

    void frame_samples_score::accumulate_grad(double g, ilat::fst const& f,
        int e) const
    {
        auto& m = autodiff::get_output<la::matrix<double>>(score);

        if (score->grad == nullptr) {
            la::matrix<double> m_grad;
            m_grad.resize(m.rows(), m.cols());
            score->grad = std::make_shared<la::matrix<double>>(std::move(m_grad));
        }

        auto& m_grad = autodiff::get_grad<la::matrix<double>>(score);

        int ell = f.output(e) - 1;
        int tail_time = f.time(f.tail(e));
        int head_time = f.time(f.head(e));

        int dur = head_time - tail_time;

        m_grad(ell, tail_time + dur * scale) += g;
    }

    void frame_samples_score::grad() const
    {
        autodiff::eval_vertex(score, autodiff::grad_funcs);
    }

    left_boundary_score::left_boundary_score(std::shared_ptr<autodiff::op_t> param,
            std::shared_ptr<autodiff::op_t> frames, int shift)
        : param(param), frames(frames), shift(shift)
    {
        score = autodiff::mmul(param, frames);
        autodiff::eval_vertex(score, autodiff::eval_funcs);
    }

    double left_boundary_score::operator()(ilat::fst const& f,
        int e) const
    {
        auto& m = autodiff::get_output<la::matrix<double>>(score);

        int ell = f.output(e) - 1;
        int tail_time = f.time(f.tail(e));
        int head_time = f.time(f.head(e));

        return m(ell, std::max<int>(tail_time + shift, 0));
    }

    void left_boundary_score::accumulate_grad(double g, ilat::fst const& f,
        int e) const
    {
        auto& m = autodiff::get_output<la::matrix<double>>(score);

        if (score->grad == nullptr) {
            la::matrix<double> m_grad;
            m_grad.resize(m.rows(), m.cols());
            score->grad = std::make_shared<la::matrix<double>>(std::move(m_grad));
        }

        auto& m_grad = autodiff::get_grad<la::matrix<double>>(score);

        int ell = f.output(e) - 1;
        int tail_time = f.time(f.tail(e));
        int head_time = f.time(f.head(e));

        m_grad(ell, std::max<int>(tail_time + shift, 0)) += g;
    }

    void left_boundary_score::grad() const
    {
        autodiff::eval_vertex(score, autodiff::grad_funcs);
    }

    right_boundary_score::right_boundary_score(std::shared_ptr<autodiff::op_t> param,
            std::shared_ptr<autodiff::op_t> frames, int shift)
        : param(param), frames(frames), shift(shift)
    {
        score = autodiff::mmul(param, frames);
        autodiff::eval_vertex(score, autodiff::eval_funcs);
    }

    double right_boundary_score::operator()(ilat::fst const& f,
        int e) const
    {
        auto& m = autodiff::get_output<la::matrix<double>>(score);

        int ell = f.output(e) - 1;
        int tail_time = f.time(f.tail(e));
        int head_time = f.time(f.head(e));

        return m(ell, std::min<int>(head_time + shift, m.cols() - 1));
    }

    void right_boundary_score::accumulate_grad(double g, ilat::fst const& f,
        int e) const
    {
        auto& m = autodiff::get_output<la::matrix<double>>(score);

        if (score->grad == nullptr) {
            la::matrix<double> m_grad;
            m_grad.resize(m.rows(), m.cols());
            score->grad = std::make_shared<la::matrix<double>>(std::move(m_grad));
        }

        auto& m_grad = autodiff::get_grad<la::matrix<double>>(score);

        int ell = f.output(e) - 1;
        int tail_time = f.time(f.tail(e));
        int head_time = f.time(f.head(e));

        m_grad(ell, std::min<int>(head_time + shift, m.cols() - 1)) += g;
    }

    void right_boundary_score::grad() const
    {
        autodiff::eval_vertex(score, autodiff::grad_funcs);
    }

    length_score::length_score(std::shared_ptr<autodiff::op_t> param)
        : param(param)
    {}

    double length_score::operator()(ilat::fst const& f,
        int e) const
    {
        int ell = f.output(e) - 1;
        int tail_time = f.time(f.tail(e));
        int head_time = f.time(f.head(e));

        auto& m = autodiff::get_output<la::matrix<double>>(param);

        return m(ell, std::min<int>(head_time - tail_time - 1, m.cols() - 1));
    }

    void length_score::accumulate_grad(double g, ilat::fst const& f,
        int e) const
    {
        auto& m = autodiff::get_output<la::matrix<double>>(param);

        if (param->grad == nullptr) {
            la::matrix<double> m_grad;
            m_grad.resize(m.rows(), m.cols());
            param->grad = std::make_shared<la::matrix<double>>(std::move(m_grad));
        }

        auto& m_grad = autodiff::get_grad<la::matrix<double>>(param);

        int ell = f.output(e) - 1;
        int tail_time = f.time(f.tail(e));
        int head_time = f.time(f.head(e));

        m_grad(ell, std::min<int>(head_time - tail_time - 1, m.cols() - 1)) += g;
    }

    log_length_score::log_length_score(std::shared_ptr<autodiff::op_t> param)
        : param(param)
    {}

    double log_length_score::operator()(ilat::fst const& f,
        int e) const
    {
        int ell = f.output(e) - 1;
        int tail_time = f.time(f.tail(e));
        int head_time = f.time(f.head(e));

        auto& m = autodiff::get_output<la::matrix<double>>(param);
        double logd = std::log(head_time - tail_time);

        return m(ell, 0) * logd + m(ell, 1) * logd * logd;
    }

    void log_length_score::accumulate_grad(double g, ilat::fst const& f,
        int e) const
    {
        auto& m = autodiff::get_output<la::matrix<double>>(param);

        if (param->grad == nullptr) {
            la::matrix<double> m_grad;
            m_grad.resize(m.rows(), m.cols());
            param->grad = std::make_shared<la::matrix<double>>(std::move(m_grad));
        }

        auto& m_grad = autodiff::get_grad<la::matrix<double>>(param);

        int ell = f.output(e) - 1;
        int tail_time = f.time(f.tail(e));
        int head_time = f.time(f.head(e));

        double logd = std::log(head_time - tail_time);

        m_grad(ell, 0) += g * logd;
        m_grad(ell, 1) += g * logd * logd;
    }

    bias_score::bias_score(std::shared_ptr<autodiff::op_t> param)
        : param(param)
    {}

    double bias_score::operator()(ilat::fst const& f,
        int e) const
    {
        int ell = f.output(e) - 1;

        auto& v = autodiff::get_output<la::vector<double>>(param);

        return v(ell);
    }

    void bias_score::accumulate_grad(double g, ilat::fst const& f,
        int e) const
    {
        auto& v = autodiff::get_output<la::vector<double>>(param);

        if (param->grad == nullptr) {
            la::vector<double> v_grad;
            v_grad.resize(v.size());
            param->grad = std::make_shared<la::vector<double>>(std::move(v_grad));
        }

        auto& v_grad = autodiff::get_grad<la::vector<double>>(param);

        int ell = f.output(e) - 1;

        v_grad(ell) += g;
    }

    std::tuple<int, std::shared_ptr<tensor_tree::vertex>, std::shared_ptr<tensor_tree::vertex>>
    load_lstm_param(std::string filename)
    {
        std::ifstream ifs { filename };
        std::string line;

        std::getline(ifs, line);
        int layer = std::stoi(line);

        std::shared_ptr<tensor_tree::vertex> nn_param
            = lstm::make_stacked_bi_lstm_tensor_tree(layer);
        tensor_tree::load_tensor(nn_param, ifs);
        std::shared_ptr<tensor_tree::vertex> pred_param = nn::make_pred_tensor_tree();
        tensor_tree::load_tensor(pred_param, ifs);

        return std::make_tuple(layer, nn_param, pred_param);
    }

    void save_lstm_param(std::shared_ptr<tensor_tree::vertex> nn_param,
        std::shared_ptr<tensor_tree::vertex> pred_param,
        std::string filename)
    {
        std::ofstream ofs { filename };

        ofs << nn_param->children.size() << std::endl;
        tensor_tree::save_tensor(nn_param, ofs);
        tensor_tree::save_tensor(pred_param, ofs);
    }

    /*
    std::vector<std::shared_ptr<autodiff::op_t>>
    make_feat(autodiff::computation_graph& comp_graph,
        std::shared_ptr<tensor_tree::vertex> lstm_var_tree,
        std::shared_ptr<tensor_tree::vertex> pred_var_tree,
        lstm::stacked_bi_lstm_nn_t& nn,
        rnn::pred_nn_t& pred_nn,
        std::vector<std::vector<double>> const& frames,
        std::default_random_engine& gen,
        inference_args& i_args)
    {
        std::vector<std::shared_ptr<autodiff::op_t>> frame_ops;
        for (auto& f: frames) {
            frame_ops.push_back(comp_graph.var(la::vector<double>(f)));
        }

        if (nn_args.dropout == 0) {
            nn = lstm::make_stacked_bi_lstm_nn(lstm_var_tree, frame_ops);
        } else {
            nn = lstm::make_stacked_bi_lstm_nn_with_dropout(comp_graph, lstm_var_tree,
                frame_ops, gen, nn_args.dropout);
        }

        if (nn_args.frame_softmax) {
            pred_nn = rnn::make_pred_nn(pred_var_tree, nn.layer.back().output);

            return pred_nn.logprob;
        } else {
            return nn.layer.back().output;
        }
    }
    */

    void parse_inference_args(inference_args& i_args,
        std::unordered_map<std::string, std::string> const& args)
    {
        i_args.args = args;

        if (ebt::in(std::string("nn-param"), args)) {
            std::tie(i_args.layer, i_args.nn_param, i_args.pred_param)
                = load_lstm_param(args.at("nn-param"));
        }

        i_args.min_seg = 1;
        if (ebt::in(std::string("min-seg"), args)) {
            i_args.min_seg = std::stoi(args.at("min-seg"));
        }

        i_args.max_seg = 20;
        if (ebt::in(std::string("max-seg"), args)) {
            i_args.max_seg = std::stoi(args.at("max-seg"));
        }

        i_args.stride = 1;
        if (ebt::in(std::string("stride"), args)) {
            i_args.stride = std::stoi(args.at("stride"));
        }

        i_args.features = ebt::split(args.at("features"), ",");

        i_args.param = make_tensor_tree(i_args.features);

        tensor_tree::load_tensor(i_args.param, args.at("param"));

        i_args.label_id = util::load_label_id(args.at("label"));

        i_args.id_label.resize(i_args.label_id.size());
        for (auto& p: i_args.label_id) {
            i_args.labels.push_back(p.second);
            i_args.id_label[p.second] = p.first;
        }
    }

    sample::sample(inference_args const& i_args)
    {
        graph_data.param = i_args.param;
    }

    void make_graph(sample& s, inference_args const& i_args)
    {
        s.graph_data.fst = make_graph(s.frames.size(), i_args.label_id, i_args.id_label, i_args.min_seg, i_args.max_seg, i_args.stride);
        s.graph_data.topo_order = std::make_shared<std::vector<int>>(
            ::fst::topo_order(*s.graph_data.fst));
    }

    void parse_learning_args(learning_args& l_args,
        std::unordered_map<std::string, std::string> const& args)
    {
        parse_inference_args(l_args, args);

        if (ebt::in(std::string("opt-data"), args)) {
            if (ebt::in(std::string("adam-beta1"), args)) {
                l_args.first_moment = make_tensor_tree(l_args.features);
                l_args.second_moment = make_tensor_tree(l_args.features);
                std::ifstream ifs { args.at("opt-data") };
                std::string line;
                std::getline(ifs, line);
                l_args.time = std::stoi(line);
                tensor_tree::load_tensor(l_args.first_moment, ifs);
                tensor_tree::load_tensor(l_args.second_moment, ifs);

            } else {
                l_args.opt_data = make_tensor_tree(l_args.features);
                tensor_tree::load_tensor(l_args.opt_data, args.at("opt-data"));
            }
        }

        if (ebt::in(std::string("nn-opt-data"), args)) {
            if (ebt::in(std::string("adam-beta1"), args)) {
                // TODO
            } else {
                std::tie(l_args.layer, l_args.nn_opt_data, l_args.pred_opt_data)
                    = load_lstm_param(args.at("nn-opt-data"));
            }
        }

        l_args.l2 = 0;
        if (ebt::in(std::string("l2"), args)) {
            l_args.l2 = std::stod(args.at("l2"));
        }

        l_args.step_size = 0;
        if (ebt::in(std::string("step-size"), args)) {
            l_args.step_size = std::stod(args.at("step-size"));
        }

        l_args.momentum = -1;
        if (ebt::in(std::string("momentum"), args)) {
            l_args.momentum = std::stod(args.at("momentum"));
            assert(0 <= l_args.momentum && l_args.momentum <= 1);
        }

        l_args.decay = -1;
        if (ebt::in(std::string("decay"), args)) {
            l_args.decay = std::stod(args.at("decay"));
            assert(0 <= l_args.decay && l_args.decay <= 1);
        }

        l_args.cost_scale = 1;
        if (ebt::in(std::string("cost-scale"), args)) {
            l_args.cost_scale = std::stod(args.at("cost-scale"));
            assert(l_args.cost_scale >= 0);
        }

        if (ebt::in(std::string("sil"), l_args.label_id)) {
            l_args.sils.push_back(l_args.label_id.at("sil"));
        }

        if (ebt::in(std::string("adam-beta1"), args)) {
            l_args.adam_beta1 = std::stod(args.at("adam-beta1"));
        }

        if (ebt::in(std::string("adam-beta2"), args)) {
            l_args.adam_beta2 = std::stod(args.at("adam-beta2"));
        }
    }

    learning_sample::learning_sample(learning_args const& l_args)
        : sample(l_args)
    {
        gold_data.param = l_args.param;
    }

    loss_func::~loss_func()
    {}

    hinge_loss::hinge_loss(fscrf_data& graph_data,
            std::vector<segcost::segment<int>> const& gt_segs,
            std::vector<int> const& sils,
            double cost_scale)
        : graph_data(graph_data), sils(sils), cost_scale(cost_scale)
    {
        auto old_weight_func = graph_data.weight_func;
        graph_data.weight_func = std::make_shared<scrf::mul<ilat::fst>>(
            scrf::mul<ilat::fst>(std::make_shared<scrf::seg_cost<ilat::fst>>(
                scrf::make_overlap_cost<ilat::fst>(gt_segs, sils)), -1));
        gold_path_data.fst = scrf::shortest_path(graph_data);
        graph_data.weight_func = old_weight_func;
        gold_path_data.weight_func = graph_data.weight_func;

        fscrf_fst gold_path { gold_path_data };

        for (auto& e: gold_path.edges()) {
            int tail_time = gold_path.time(gold_path.tail(e));
            int head_time = gold_path.time(gold_path.head(e));

            gold_segs.push_back(segcost::segment<int> { tail_time, head_time, gold_path.output(e) });
        }

        graph_data.cost_func = std::make_shared<scrf::mul<ilat::fst>>(
            scrf::mul<ilat::fst>(std::make_shared<scrf::seg_cost<ilat::fst>>(
                scrf::make_overlap_cost<ilat::fst>(gold_segs, sils)), cost_scale));

        gold_path_data.cost_func = graph_data.cost_func;

        auto& id_symbol = *graph_data.fst->data->id_symbol;

        double gold_cost = 0;
        double gold_score = 0;
        std::cout << "gold:";
        for (auto& e: gold_path.edges()) {
            double c = (*gold_path_data.cost_func)(*gold_path_data.fst, e);
            gold_cost += c;
            gold_score += gold_path.weight(e);

            std::cout << " " << id_symbol[gold_path.output(e)] << " (" << c << ")";
        }
        std::cout << std::endl;
        std::cout << "gold cost: " << gold_cost << std::endl;
        std::cout << "gold score: " << gold_score << std::endl;

        scrf::composite_weight<ilat::fst> weight_cost;
        weight_cost.weights.push_back(graph_data.cost_func);
        weight_cost.weights.push_back(graph_data.weight_func);
        graph_data.weight_func = std::make_shared<scrf::composite_weight<ilat::fst>>(weight_cost);
        graph_path_data.fst = scrf::shortest_path(graph_data);
        graph_path_data.weight_func = graph_data.weight_func;

        fscrf_fst graph_path { graph_path_data };

        double cost_aug_cost = 0;
        double cost_aug_score = 0;
        std::cout << "cost aug:";
        for (auto& e: graph_path.edges()) {
            cost_aug_cost += (*graph_data.cost_func)(*graph_path_data.fst, e);
            cost_aug_score += graph_path.weight(e);

            std::cout << " " << id_symbol[graph_path.output(e)];
        }
        std::cout << std::endl;
        std::cout << "cost aug cost: " << cost_aug_cost << std::endl;
        std::cout << "cost aug score: " << cost_aug_score << std::endl;
    }

    double hinge_loss::loss() const
    {
        double result = 0;

        fscrf_fst gold_path { gold_path_data };

        for (auto& e: gold_path.edges()) {
            result -= gold_path.weight(e);
        }

        fscrf_fst graph_path { graph_path_data };

        for (auto& e: graph_path.edges()) {
            result += graph_path.weight(e);
        }

        return result;
    }

    void hinge_loss::grad() const
    {
        fscrf_fst gold_path { gold_path_data };

        for (auto& e: gold_path.edges()) {
            gold_path_data.weight_func->accumulate_grad(-1, *gold_path_data.fst, e);
        }

        fscrf_fst graph_path { graph_path_data };

        for (auto& e: graph_path.edges()) {
            graph_path_data.weight_func->accumulate_grad(1, *graph_path_data.fst, e);
        }
    }

    log_loss::log_loss(fscrf_data& graph_data,
        std::vector<segcost::segment<int>> const& gt_segs,
        std::vector<int> const& sils)
        : graph_data(graph_data)
    {
        auto old_weight_func = graph_data.weight_func;
        graph_data.weight_func = std::make_shared<scrf::mul<ilat::fst>>(
            scrf::mul<ilat::fst>(std::make_shared<scrf::seg_cost<ilat::fst>>(
                scrf::make_overlap_cost<ilat::fst>(gt_segs, sils)), -1));
        gold_path_data.fst = scrf::shortest_path(graph_data);
        graph_data.weight_func = old_weight_func;
        gold_path_data.weight_func = graph_data.weight_func;

        fscrf_fst gold_path { gold_path_data };

        for (auto& e: gold_path.edges()) {
            int tail_time = gold_path.time(gold_path.tail(e));
            int head_time = gold_path.time(gold_path.head(e));

            gold_segs.push_back(segcost::segment<int> { tail_time, head_time, gold_path.output(e) });
        }

        gold_path_data.cost_func = std::make_shared<scrf::mul<ilat::fst>>(
            scrf::mul<ilat::fst>(std::make_shared<scrf::seg_cost<ilat::fst>>(
                scrf::make_overlap_cost<ilat::fst>(gold_segs, sils)), 1));

        auto& id_symbol = *graph_data.fst->data->id_symbol;

        double gold_cost = 0;
        double gold_score = 0;
        std::cout << "gold:";
        for (auto& e: gold_path.edges()) {
            double c = (*gold_path_data.cost_func)(*gold_path_data.fst, e);
            gold_cost += c;
            gold_score += gold_path.weight(e);

            std::cout << " " << id_symbol[gold_path.output(e)] << " (" << c << ")";
        }
        std::cout << std::endl;
        std::cout << "gold cost: " << gold_cost << std::endl;
        std::cout << "gold score: " << gold_score << std::endl;

        fscrf_fst graph { graph_data };

        forward.merge(graph, *graph_data.topo_order);

        auto rev_topo_order = *graph_data.topo_order;
        std::reverse(rev_topo_order.begin(), rev_topo_order.end());

        backward.merge(graph, rev_topo_order);

        for (auto& f: graph.finals()) {
            std::cout << "forward: " << forward.extra[f] << std::endl;
        }

        for (auto& i: graph.initials()) {
            std::cout << "backward: " << backward.extra[i] << std::endl;
        }
    }

    double log_loss::loss() const
    {
        double result = 0;

        fscrf_fst gold_path { gold_path_data };

        for (auto& e: gold_path.edges()) {
            result -= gold_path.weight(e);
        }

        fscrf_fst graph { graph_data };

        result += forward.extra.at(graph.finals().front());

        return result;
    }

    void log_loss::grad() const
    {
        fscrf_fst gold_path { gold_path_data };

        for (auto& e: gold_path.edges()) {
            gold_path_data.weight_func->accumulate_grad(-1, *gold_path_data.fst, e);
        }

        fscrf_fst graph { graph_data };

        double logZ = forward.extra.at(graph.finals().front());

        for (auto& e: graph.edges()) {
            graph_data.weight_func->accumulate_grad(
                std::exp(forward.extra.at(graph.tail(e)) + graph.weight(e)
                    + backward.extra.at(graph.head(e)) - logZ), *graph_data.fst, e);
        }
    }

    double mode2_weight::operator()(ilat::pair_fst const& fst,
        std::tuple<int, int> e) const
    {
        return (*weight)(fst.fst2(), std::get<1>(e));
    }

    void mode2_weight::accumulate_grad(double g, ilat::pair_fst const& fst,
        std::tuple<int, int> e) const
    {
        weight->accumulate_grad(g, fst.fst2(), std::get<1>(e));
    }

    void mode2_weight::grad() const
    {
        weight->grad();
    }

}
