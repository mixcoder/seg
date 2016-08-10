#include "scrf/fscrf.h"
#include "scrf/loss.h"
#include "scrf/scrf_weight.h"
#include "scrf/util.h"
#include <fstream>

struct prediction_env {

    std::ifstream lat_batch;

    std::shared_ptr<tensor_tree::vertex> param;

    std::vector<std::string> features;

    std::vector<std::string> id_label;
    std::unordered_map<std::string, int> label_id;

    std::unordered_map<std::string, std::string> args;

    prediction_env(std::unordered_map<std::string, std::string> args);

    void run();

};

int main(int argc, char *argv[])
{
    ebt::ArgumentSpec spec {
        "predict-order1-lat",
        "Predict with segmental CRF",
        {
            {"lat-batch", "", true},
            {"param", "", true},
            {"features", "", true},
            {"label", "", true},
        }
    };

    if (argc == 1) {
        ebt::usage(spec);
        exit(1);
    }

    auto args = ebt::parse_args(argc, argv, spec);

    for (int i = 0; i < argc; ++i) {
        std::cout << argv[i] << " ";
    }
    std::cout << std::endl;

    prediction_env env { args };

    env.run();

    return 0;
}

prediction_env::prediction_env(std::unordered_map<std::string, std::string> args)
    : args(args)
{
    lat_batch.open(args.at("lat-batch"));

    features = ebt::split(args.at("features"), ",");

    param = fscrf::lat::make_tensor_tree(features);
    tensor_tree::load_tensor(param, args.at("param"));

    label_id = util::load_label_id(args.at("label"));
    id_label.resize(label_id.size());
    for (auto& p: label_id) {
        id_label[p.second] = p.first;
    }
}

void prediction_env::run()
{
    ebt::Timer timer;

    int i = 0;

    while (1) {

        ilat::fst lat = ilat::load_lattice(lat_batch, label_id);

        if (!lat_batch) {
            break;
        }

        fscrf::fscrf_data graph_data;
        graph_data.topo_order = std::make_shared<std::vector<int>>(fst::topo_order(lat));
        graph_data.fst = std::make_shared<ilat::fst>(lat);

        autodiff::computation_graph comp_graph;
        std::shared_ptr<tensor_tree::vertex> var_tree = tensor_tree::make_var_tree(comp_graph, param);

        graph_data.weight_func = fscrf::lat::make_weights(features, var_tree);

        fscrf::fscrf_fst scrf { graph_data };

        fst::forward_one_best<fscrf::fscrf_fst> one_best;

        for (auto& i: scrf.initials()) {
            one_best.extra[i] = fst::forward_one_best<fscrf::fscrf_fst>::extra_data {-1, 0};
        }

        one_best.merge(scrf, *graph_data.topo_order);

        std::vector<int> paths = one_best.best_path(scrf);

        for (auto& e: paths) {
            std::cout << id_label.at(scrf.output(e)) << " ";
        }
        std::cout << "(" << lat.data->name << ")" << std::endl;

        ++i;

#if DEBUG_TOP
        if (i == DEBUG_TOP) {
            break;
        }
#endif

    }

}

