/* -*- mode: c++; tab-width: 4; indent-tabs-mode: nil; c-basic-offset: 4 -*- */

/*
 Copyright (C) 2008 Andreas Gaida
 Copyright (C) 2008, 2009 Ralph Schreyer
 Copyright (C) 2008, 2009 Klaus Spanderen

 This file is part of QuantLib, a free-software/open-source library
 for financial quantitative analysts and developers - http://quantlib.org/

 QuantLib is free software: you can redistribute it and/or modify it
 under the terms of the QuantLib license.  You should have received a
 copy of the license along with this program; if not, please email
 <quantlib-dev@lists.sf.net>. The license is also available online at
 <http://quantlib.org/license.shtml>.

 This program is distributed in the hope that it will be useful, but WITHOUT
 ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS
 FOR A PARTICULAR PURPOSE.  See the license for more details.
*/

#include <ql/processes/blackscholesprocess.hpp>
#include <ql/math/interpolations/cubicinterpolation.hpp>
#include <ql/methods/finitedifferences/finitedifferencemodel.hpp>
#include <ql/methods/finitedifferences/meshers/fdmmesher.hpp>
#include <ql/methods/finitedifferences/operators/fdmlinearoplayout.hpp>
#include <ql/methods/finitedifferences/stepconditions/fdmstepconditioncomposite.hpp>
#include <ql/methods/finitedifferences/utilities/fdminnervaluecalculator.hpp>
#include <ql/methods/finitedifferences/stepconditions/fdmsnapshotcondition.hpp>
#include <ql/methods/finitedifferences/solvers/fdmbackwardsolver.hpp>
#include <ql/methods/finitedifferences/operators/fdmblackscholesop.hpp>
#include <ql/methods/finitedifferences/solvers/fdmblackscholessolver.hpp>

namespace QuantLib {

    FdmBlackScholesSolver::FdmBlackScholesSolver(
        const Handle<GeneralizedBlackScholesProcess>& process,
        Real strike,
        const FdmSolverDesc& solverDesc,
        const FdmSchemeDesc& schemeDesc,
        bool localVol,
        Real illegalLocalVolOverwrite)
    : process_(process),
      strike_(strike),
      solverDesc_(solverDesc),
      schemeDesc_(schemeDesc),
      mesher_(solverDesc.mesher),
      thetaCondition_(new FdmSnapshotCondition(
        0.99*std::min(1.0/365.0,
                   solverDesc.condition->stoppingTimes().empty()
                       ? solverDesc.maturity
                       : solverDesc.condition->stoppingTimes().front()))),
      conditions_(FdmStepConditionComposite::joinConditions(thetaCondition_,
                                                      solverDesc.condition)),
      localVol_(localVol),
      illegalLocalVolOverwrite_(illegalLocalVolOverwrite),
      initialValues_(mesher_->layout()->size()),
      resultValues_(mesher_->layout()->dim()[0]) {

        registerWith(process_);

        x_.reserve(mesher_->layout()->dim()[0]);

        const boost::shared_ptr<FdmLinearOpLayout> layout = mesher_->layout();
        const FdmLinearOpIterator endIter = layout->end();
        for (FdmLinearOpIterator iter = layout->begin(); iter != endIter;
             ++iter) {
            initialValues_[iter.index()]
                 = solverDesc.calculator->avgInnerValue(iter,
                                                        solverDesc.maturity);
            x_.push_back(mesher_->location(iter, 0));
        }
    }

    void FdmBlackScholesSolver::performCalculations() const {
        boost::shared_ptr<FdmBlackScholesOp> map(new FdmBlackScholesOp(
                mesher_, process_.currentLink(), strike_,
                localVol_, illegalLocalVolOverwrite_));

        Array rhs(initialValues_.size());
        std::copy(initialValues_.begin(), initialValues_.end(), rhs.begin());

        FdmBackwardSolver(map, solverDesc_.bcSet, conditions_, schemeDesc_)
            .rollback(rhs, solverDesc_.maturity, 0.0,
                      solverDesc_.timeSteps, solverDesc_.dampingSteps);

        std::copy(rhs.begin(), rhs.end(), resultValues_.begin());

        interpolation_ = boost::shared_ptr<CubicInterpolation>(new
            MonotonicCubicNaturalSpline(x_.begin(), x_.end(),
                                        resultValues_.begin()));
    }

    Real FdmBlackScholesSolver::valueAt(Real s) const {
        calculate();
        return interpolation_->operator()(std::log(s));
    }

    Real FdmBlackScholesSolver::deltaAt(Real s) const {
        calculate();
        return interpolation_->derivative(std::log(s))/s;
    }

    Real FdmBlackScholesSolver::gammaAt(Real s) const {
        calculate();
        return (interpolation_->secondDerivative(std::log(s))
                -interpolation_->derivative(std::log(s)))/(s*s);
    }

    Real FdmBlackScholesSolver::thetaAt(Real s) const {
        QL_REQUIRE(conditions_->stoppingTimes().front() > 0.0,
                   "stopping time at zero-> can't calculate theta");

        calculate();
        Array thetaValues(resultValues_.size());

        const Array& rhs = thetaCondition_->getValues();
        std::copy(rhs.begin(), rhs.end(), thetaValues.begin());

        Real temp = MonotonicCubicNaturalSpline(
            x_.begin(), x_.end(), thetaValues.begin())(std::log(s)); 
        return ( temp - valueAt(s) ) / thetaCondition_->getTime();
    }
}
