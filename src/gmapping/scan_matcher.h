#ifndef SCAN_MATCHER_H
#define SCAN_MATCHER_H

#include <math.h>
#include <iostream>

#include "../core/laser_scan_grid_world.h"
#include "../core/maps/grid_map.h"
#include "gmapping_world.h"

class TrigonometricCache {
public:
  TrigonometricCache() :
    _sin_theta(0), _cos_theta(0),
    _angle_min(0), _angle_max(0), _angle_delta(0) {}

  inline double sin(int angle_idx) const {
    return _sin_theta * _cos[angle_idx] + _cos_theta * _sin[angle_idx];
  }

  inline double cos(int angle_idx) const {
    return _cos_theta * _cos[angle_idx] - _sin_theta * _sin[angle_idx];
  }

  void setTheta(double theta) {
    _sin_theta = std::sin(theta);
    _cos_theta = std::cos(theta);
  }

  void createCache(double a_min, double a_max, double a_inc) {
    if(a_min == _angle_min && a_max == _angle_max && a_inc == _angle_delta)
      return;

    _sin.clear();
    _cos.clear();
    _angle_min = a_min;
    _angle_max = a_max;
    _angle_delta = a_inc;
    for(double angle = _angle_min; angle < _angle_max; angle += _angle_delta) {
      _sin.push_back(std::sin(angle));
      _cos.push_back(std::cos(angle));
    }
  }

private:
  std::vector<double> _sin, _cos;
  double _sin_theta, _cos_theta;
  double _angle_min, _angle_max, _angle_delta;
};

class ScanMatcher {
public:
  ScanMatcher() : angularStep(0.1), linearStep(0.1), recursiveIterations(5),
    laserScanMargin(0), laserScanSkipRate(0), windowSize(1),
    maxRegScore(1000.0), minRegScore(0.0), firstScan(true) {}

  double processScan(const RobotPose &initPose,
                     const TransformedLaserScan &scan,
                     GridMap &map,
                     RobotPose &poseDelta) {
    //angleCache.createCache(scan.angle_min, scan.angle_max, scan.angle_delta);

    RobotPose refinedPose;
    poseDelta = RobotPose();
    double refinedLikelihood = 0.0;
    double refinedScore = optimize(initPose, scan, map, refinedPose, refinedLikelihood);
//    std::cout << "Refined score: " << refinedScore << " | "  << refinedPose.x << ", " << refinedPose.y << ", " << refinedPose.theta << std::endl;
    if(refinedScore > minRegScore || firstScan) {
      registerScan(refinedPose, scan, map);
      poseDelta = RobotPose(refinedPose.x - initPose.x,
                             refinedPose.y - initPose.y,
                             refinedPose.theta - initPose.theta);
      firstScan = false;
    }
    /*
    if (refinedScore < maxRegScore) {
      m_matcher.invalidateActiveArea();
      if (refinedScore < minRegScore) {
        registerScan(initPose, scan);
      } else {
        registerScan(refinedPose, scan);
        newPose = refinedPose;
      }
    }*/

    return refinedLikelihood;
  }

private:
  double angularStep, linearStep;
  int recursiveIterations, laserScanMargin, laserScanSkipRate, windowSize;
  double maxRegScore, minRegScore;
  RobotPose laserOffset;

  mutable TrigonometricCache angleCache;
  bool firstScan;

  double optimize(const RobotPose &initPose,
                  const TransformedLaserScan &scan,
                  const GridMap &map,
                  RobotPose &newPose,
                  double &newPoseLikelihood) const {
    RobotPose currentPose = initPose;
    double adelta = angularStep;
    double ldelta = linearStep;
    double bestLikelihood = 0.0;
    double bestScore = -1;

    double currentScore = score(currentPose, scan, map, bestLikelihood);
    //  std::cout << "Initial score: " << currentScore << " | "  << initPose.x << ", " << initPose.y << ", " << initPose.theta << std::endl;

    enum Move { Front, Back, Left, Right, TurnLeft, TurnRight, Done };

    for(int iter = 0; currentScore > bestScore || iter < recursiveIterations;) {
      RobotPose bestLocalPose, localPose;
      bestLocalPose = localPose = currentPose;

      if(bestScore >= currentScore) {
        ++iter;
        adelta *= 0.5;
        ldelta *= 0.5;
      }
      bestScore = currentScore;

      for(Move move = Front; move != Done;) {
        localPose = currentPose;
        switch(move) {
        case Front:
          localPose.x += ldelta;
          move = Back;
          break;
        case Back:
          localPose.x -= ldelta;
          move = Left;
          break;
        case Left:
          localPose.y -= ldelta;
          move = Right;
          break;
        case Right:
          localPose.y += ldelta;
          move = TurnLeft;
          break;
        case TurnLeft:
          localPose.theta += adelta;
          move = TurnRight;
          break;
        case TurnRight:
          localPose.theta -= adelta;
          move = Done;
          break;
        default:
          break;
        }

        // m_angularOdometryReliability and m_linearOdometryReliability are defaulted to 0 in gmapping
                /*
                double odo_gain=1;
                if (m_angularOdometryReliability>0.){
                    double dth=init.theta-localPose.theta; dth=atan2(sin(dth), cos(dth)); dth*=dth;
                    odo_gain*=exp(-m_angularOdometryReliability*dth);
                }
                if (m_linearOdometryReliability>0.){
                    double dx=init.x-localPose.x;
                    double dy=init.y-localPose.y;
                    double drho=dx*dx+dy*dy;
                    odo_gain*=exp(-m_linearOdometryReliability*drho);
                }
                double localScore=odo_gain*score(map, localPose, readings);
                */

        double likelihood = 0.0;
        double localScore = score(localPose, scan, map, likelihood);
        if (localScore > currentScore){
          currentScore = localScore;
          bestLocalPose = localPose;
          bestLikelihood = likelihood;
        }
      }
      currentPose = bestLocalPose;
    }
    newPose = currentPose;
    newPoseLikelihood = bestLikelihood;
    return bestScore;
  }

  RobotPose getLaserPose(const RobotPose &robotPose) const {
    if(!laserOffset.x && !laserOffset.y && !laserOffset.theta) {
      return robotPose;
    }
    RobotPose laserPose = robotPose;
    double c = cos(robotPose.theta), s = sin(robotPose.theta);
    laserPose.x += c * laserOffset.x - s * laserOffset.y;
    laserPose.y += s * laserOffset.x + c * laserOffset.y;
    laserPose.theta += laserOffset.theta;
    return laserPose;
  }

  inline double length(double x, double y) const {
    return x * x + y * y;
  }

  double score(const RobotPose &pose,
               const TransformedLaserScan &scan,
               const GridMap &map,
               double &likelihood) const {
    static const double m_fullnessThreshold = 0.2; //hardcoded in gmapping (0.1)
    static const double m_gaussianSigma = 0.01;    //default value in ref. impl.
//        double m_likelihoodSigma = 0.075;   //default value in ros gmapping
//        double m_nullLikelihood = -0.5;     //hardcoded in gmapping

    // looks like hole_width in tinySlam;
    // NB: map.cellSize()^2 * sqrt(2.0) in gmapping
    static const double freeDelta = sqrt(2.0);
//        double noHit = m_nullLikelihood / m_likelihoodSigma;

    double resScore = 0.0;
    int skip = 0;
    likelihood = 0.0;
    RobotPose laserPose = getLaserPose(pose);
    angleCache.setTheta(laserPose.theta);
    for (unsigned int i = laserScanMargin;
         i < scan.points.size() - laserScanMargin; ++i) {

      skip = ++skip > laserScanSkipRate ? 0 : skip;
      if (skip) continue;

      const double &r = scan.points[i].range;
      const double angle = scan.points[i].angle;
//            if (r < scan.range_min || r > scan.range_max) continue;
      double c = std::cos(angle); //angleCache.cos(i);
      double s = std::sin(angle); //angleCache.sin(i);
      double spx = laserPose.x + r * c;
      double spy = laserPose.y + r * s;
      DiscretePoint2D scanPoint = map.world_to_cell(spx, spy);
      DiscretePoint2D freePoint = -DiscretePoint2D(round(freeDelta * c),
                                                   round(freeDelta * s));
      bool found = false;
      double bestDist = 0.0;
      for (int xx = -windowSize; xx <= windowSize; ++xx) {
        for (int yy = -windowSize; yy <= windowSize; ++yy) {
          DiscretePoint2D testPoint = scanPoint + DiscretePoint2D(xx, yy);
          //if(!map.has_cell(pr) || !map.has_cell(pf)) continue;
          const GridMap::Cell &cell = map.cell(testPoint);
          const GridMap::Cell &fcell = map.cell(testPoint.x + freePoint.x,
                                                testPoint.y + freePoint.y);
          if (fcell->value() < m_fullnessThreshold &&
              m_fullnessThreshold <= cell->value()) {
            double dist = length(spx - cell->obst_x(), spy - cell->obst_y());
            if (dist < bestDist || !found) {
              bestDist = dist;
              found = true;
            }
          }
        }
      }
      if (found) {
//                double f = -20.0 * bestDist + 1.0;
//                if(f >= 0) {
//                    resScore += f;
//                resScore += 1.0 / (250.0 * bestDist + 1.0);
        resScore += exp(-1.0 / m_gaussianSigma * bestDist);
        ++likelihood;
//                }
      }
//            likelihood += found ? -1.0 / m_likelihoodSigma * mu : noHit;
    }
    return resScore;
  }

  void registerScan(const RobotPose &pose,
                    const TransformedLaserScan &scan,
                    GridMap &map) {
    RobotPose laserPose = getLaserPose(pose);
    angleCache.setTheta(laserPose.theta);
    DiscretePoint2D p0 = map.world_to_cell(laserPose.x , laserPose.y);

    for(unsigned int i = laserScanMargin;
        i < scan.points.size() - laserScanMargin; ++i) {

      const double &r = scan.points[i].range;
      const double angle = scan.points[i].angle;
//            if(d < scan.range_min || d > scan.range_max) continue;
//            double spx = laserPose.x + r * angleCache.cos(i);
//            double spy = laserPose.y + r * angleCache.sin(i);
      double spx = laserPose.x + r * std::cos(angle);
      double spy = laserPose.y + r * std::sin(angle);

      DiscretePoint2D p1 = map.world_to_cell(spx, spy);

      DiscreteLine2D line = DiscreteLine2D(p0, p1);
      map.cell(line.points().back())->set_value(Occupancy{1.0, 1.0, spx, spy});
      for (auto p = line.points().begin(); p != line.points().end() - 1; ++p) {
        map.cell(*p)->set_value(Occupancy{0.0, 1.0, 0.0, 0.0});
      }
    }
  }

};

#endif
