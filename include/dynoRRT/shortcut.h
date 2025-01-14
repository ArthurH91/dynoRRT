#include "dynoRRT/dynorrt_macros.h"
#include "dynotree/KDTree.h"
#include "nlohmann/json.hpp"
#include <Eigen/Dense>
#include <chrono>
#include <iostream>
#include <toml.hpp>

namespace dynorrt {

template <typename StateSpace, int DIM> class PathShortCut {

  using json = nlohmann::json;

public:
  using state_t = Eigen::Matrix<double, DIM, 1>;
  using state_ref_t = Eigen::Ref<state_t>;
  using state_cref_t = const Eigen::Ref<const state_t> &;
  using edge_t = std::pair<state_t, state_t>;
  using is_collision_free_fun_t = std::function<bool(state_t)>;

  virtual ~PathShortCut() = default;

  void set_state_space(StateSpace t_state_space) {
    state_space = t_state_space;
  }

  void set_state_space_with_string(
      const std::vector<std::string> &state_space_vstring) {
    if constexpr (std::is_same<StateSpace, dynotree::Combined<double>>::value) {
      state_space = StateSpace(state_space_vstring);
    } else {
      THROW_PRETTY_DYNORRT("use set_state_string only with dynotree::Combined");
    }
  }

  virtual void print_options(std::ostream &out = std::cout) {
    THROW_PRETTY_DYNORRT("Not implemented Yet");
  }

  void virtual set_options_from_toml(toml::value &cfg) {
    THROW_PRETTY_DYNORRT("Not implemented");
  }

  virtual void read_cfg_file(const std::string &cfg_file) {
    THROW_PRETTY_DYNORRT("Not implemented");
  }

  void virtual read_cfg_string(const std::string &cfg_string) {
    THROW_PRETTY_DYNORRT("Not implemented");
  }

  void set_bounds_to_state(const Eigen::VectorXd &lb,
                           const Eigen::VectorXd &ub) {
    CHECK_PRETTY_DYNORRT__(lb.size() == ub.size());

    if constexpr (DIM == -1) {
      CHECK_PRETTY_DYNORRT__(runtime_dim != -1);
      CHECK_PRETTY_DYNORRT__(lb.size() == runtime_dim);
      CHECK_PRETTY_DYNORRT__(ub.size() == runtime_dim);
    }

    state_space.set_bounds(lb, ub);
  }

  void
  set_is_collision_free_fun(is_collision_free_fun_t t_is_collision_free_fun) {
    is_collision_free_fun = t_is_collision_free_fun;
  }

  // void
  // set_collision_manager(CollisionManagerBallWorld<DIM> *collision_manager) {
  //   is_collision_free_fun = [collision_manager](state_t x) {
  //     return !collision_manager->is_collision(x);
  //   };
  // }

  // TODO: timing collisions take a lot of overhead, specially for
  // very simple envs where collisions are very fast.
  bool is_collision_free_fun_timed(state_cref_t x) {
    auto tic = std::chrono::steady_clock::now();
    bool is_collision_free = is_collision_free_fun(x);
    double elapsed_ns = std::chrono::duration_cast<std::chrono::nanoseconds>(
                            std::chrono::steady_clock::now() - tic)
                            .count();
    collisions_time_ms += elapsed_ns / 1e6;
    number_collision_checks++;
    return is_collision_free;
  }

  StateSpace &get_state_space() { return state_space; }

  std::vector<state_t> get_path() {

    if (path.size() == 0) {
      std::cout << "Warning: path.size() == 0" << std::endl;
      std::cout << __FILE__ << ":" << __LINE__ << std::endl;
      return {};
    }
    return path;
  }

  virtual void init(int t_runtime_dim = -1) {

    runtime_dim = t_runtime_dim;

    if constexpr (DIM == -1) {
      if (runtime_dim == -1) {
        throw std::runtime_error("DIM == -1 and runtime_dim == -1");
      }
    }
  }

  std::vector<state_t> get_fine_path(double resolution) {

    state_t tmp;

    if constexpr (DIM == -1) {
      if (runtime_dim == -1) {
        throw std::runtime_error("DIM == -1 and runtime_dim == -1");
      }
      tmp.resize(runtime_dim);
    }

    std::vector<state_t> fine_path;
    if (path.size() == 0) {
      std::cout << "Warning: path.size() == 0" << std::endl;
      std::cout << __FILE__ << ":" << __LINE__ << std::endl;
      return {};
    }

    for (int i = 0; i < path.size() - 1; i++) {
      state_t _start = path[i];
      state_t _goal = path[i + 1];
      int N = int(state_space.distance(_start, _goal) / resolution) + 1;
      for (int j = 0; j < N; j++) {
        state_space.interpolate(_start, _goal, double(j) / N, tmp);
        fine_path.push_back(tmp);
      }
    }
    fine_path.push_back(path[path.size() - 1]);
    return fine_path;
  }

  virtual std::string get_name() { return "PathShortCut"; }

  virtual void get_planner_data(json &j) {
    j["planner_name"] = this->get_name();
    j["path"] = path;
    j["fine_path"] = get_fine_path(0.01);
    j["evaluated_edges"] = evaluated_edges;
    j["infeasible_edges"] = infeasible_edges;
    j["total_distance"] = total_distance;
    j["collisions_time_ms"] = collisions_time_ms;
    j["number_collision_checks"] = number_collision_checks;
    j["valid_edges"] = valid_edges;
    j["invalid_edges"] = invalid_edges;

    // THROW_PRETTY_DYNORRT("Not implemented in base class!");
  }

  virtual void check_internal() const {

    CHECK_PRETTY_DYNORRT__(path.size() == 0);
    CHECK_PRETTY_DYNORRT__(evaluated_edges == 0);
    CHECK_PRETTY_DYNORRT__(infeasible_edges == 0);
    CHECK_PRETTY_DYNORRT__(total_distance == -1);
    CHECK_PRETTY_DYNORRT__(collisions_time_ms == 0);
  }

  virtual void reset_internal() {
    path.clear();
    evaluated_edges = 0;
    infeasible_edges = 0;
    total_distance = -1;
    collisions_time_ms = 0;
    init();
  }

  virtual void set_initial_path(const std::vector<state_t> &t_path) {
    initial_path = t_path;
  }

  virtual void shortcut() {
    CHECK_PRETTY_DYNORRT__(initial_path.size() >= 2);

    if (initial_path.size() == 2) {
      // [ start, goal ]
      path = initial_path;
    }

    path.clear();
    int start_index = 0;
    path.push_back(initial_path.at(start_index));
    while (true) {
      int target_index = start_index + 2;
      // We know that +1 is always feasible!

      auto is_edge_collision_free_ = [&] {
        bool out = is_edge_collision_free(
            initial_path[start_index], initial_path[target_index],
            is_collision_free_fun, state_space, resolution);
        evaluated_edges++;
        if (!out) {
          infeasible_edges++;
        }
        return out;
      };

      while (target_index < initial_path.size() && is_edge_collision_free_()) {
        target_index++;
      }
      target_index--; // Reduce one, to get the last collision free edge.

      CHECK_PRETTY_DYNORRT__(target_index >= start_index + 1);
      CHECK_PRETTY_DYNORRT__(target_index < initial_path.size());

      path.push_back(initial_path[target_index]);

      if (target_index == initial_path.size() - 1) {
        break;
      }

      start_index = target_index;
    }

    CHECK_PRETTY_DYNORRT__(path.size() >= 2);
    CHECK_PRETTY_DYNORRT__(path.size() <= initial_path.size());

    MESSAGE_PRETTY_DYNORRT("\nPath_shortcut: Num of waypoints Reduced From "
                           << initial_path.size() << " to " << path.size()
                           << "\n");

    double total_distance_before = get_path_length(initial_path, state_space);
    total_distance = get_path_length(path, state_space);

    MESSAGE_PRETTY_DYNORRT("\nPath_shortcut: Distance reduced from "
                           << total_distance_before << " to " << total_distance
                           << "\n");
  }

protected:
  StateSpace state_space;
  // User can define a goal or goal_list.
  // NOTE: Goal list has priority over goal
  is_collision_free_fun_t is_collision_free_fun = [](const auto &) {
    THROW_PRETTY_DYNORRT("You have to define a collision free fun!");
    return false;
  };
  std::vector<state_t> initial_path;
  std::vector<state_t> path;
  int runtime_dim = DIM;
  double total_distance = -1;
  double collisions_time_ms = 0.;
  int number_collision_checks = 0;
  int evaluated_edges = 0;
  int infeasible_edges = 0;
  double resolution = .05;
  std::vector<std::pair<state_t, state_t>>
      valid_edges; // TODO: only with a flag
  std::vector<std::pair<state_t, state_t>>
      invalid_edges; // TODO: only rrth a flag
  //
};
}; // namespace dynorrt
