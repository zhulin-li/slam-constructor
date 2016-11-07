#ifndef __GMAPPING_COST_ESTIMATOR
#define __GMAPPING_COST_ESTIMATOR

#include <cmath>
#include <limits>
#include "../core/geometry_utils.h"
#include "../core/grid_scan_matcher.h"
#include "gmapping_world.h"

class GmappingCostEstimator : public ScanCostEstimator {
private:
  constexpr static double FULLNESS_TH = 0.5;
  constexpr static double SIGMA_SQ = 0.01;
  constexpr static double FREE_CELL_DIST = std::sqrt(2.0);
  constexpr static double DBL_INF = std::numeric_limits<double>::infinity();
  using CellValue = const GmappingCellValue &;
public:
  GmappingCostEstimator() : _scan_margin(0), _pts_skip_rate(3), _window_sz(1) {}

  virtual double estimate_scan_cost(const RobotPose &pose,
                                    const TransformedLaserScan &scan,
                                    const GridMap &map,
                                    double min_cost = 0) {
    double scan_weight = 0, last_dpoint_weight = -1;
    DiscretePoint2D last_handled_dpoint;

    scan.trig_cache->set_theta(pose.theta);
    for (size_t i = _scan_margin; i < scan.points.size() - _scan_margin; ++i) {
      if (_pts_skip_rate && i % _pts_skip_rate) {
        continue;
      }

      const ScanPoint &sp = scan.points[i];
      double c = scan.trig_cache->cos(sp.angle);
      double s = scan.trig_cache->sin(sp.angle);
      const Point2D sp_world(pose.x + sp.range * c, pose.y + sp.range * s);

      const DiscretePoint2D sp_coord = map.world_to_cell(sp_world.x,
                                                         sp_world.y);
      if (sp_coord == last_handled_dpoint && last_dpoint_weight != -1) {
        scan_weight += last_dpoint_weight;
        continue;
      }

      last_handled_dpoint = sp_coord;
      double best_dist = DBL_INF;
      double d_free_x = FREE_CELL_DIST * c; // 0< int cast - implicit floor
      double d_free_y = FREE_CELL_DIST * s;
      for (int d_x = -_window_sz; d_x <= _window_sz; ++d_x) {
        for (int d_y = -_window_sz; d_y <= _window_sz; ++d_y) {
          DiscretePoint2D cell = sp_coord + DiscretePoint2D(d_x, d_y);
          const GridCellValue &cell_value = map[cell];
          if (cell_value < FULLNESS_TH) {
            continue; // cell is not occupied
          }
          cell.x -= d_free_x;
          cell.y -= d_free_y;
          const GridCellValue &free_cell_val = map[cell];

          if (FULLNESS_TH <= free_cell_val) {
            continue; //occlusion is detected
          }

          // NB: static (not dynamic) cast is used for performance reasons
          CellValue gmg_val = static_cast<CellValue>(cell_value);
          double dist = sp_world.dist_sq(gmg_val.obst);
          if (dist < best_dist) {
            best_dist = dist;
          }
        }
      }
      double dist_sq = best_dist != DBL_INF ? best_dist : 9 * SIGMA_SQ;
      last_dpoint_weight = exp(-dist_sq / SIGMA_SQ);
      scan_weight += last_dpoint_weight;
    }

    return scan_weight;
  }

private:
  int _scan_margin, _pts_skip_rate, _window_sz;
};

#endif