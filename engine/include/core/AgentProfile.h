#pragma once
// ONE agent for every walkability oracle (WalkabilityGateAndPlaytestLoop increment 4).
// The settlement gate's TraversalProbe box, the runtime NavGraph's NavAgentProfile and
// the player capsule must agree, or three oracles can only disagree. Grounded in
// AnimatedVoxelCharacter: half-width m_originalHalfWidth = 0.25 m (-> 2 micro either
// side of centre), standing height = the humanoid model 1.82 m + kControllerHeadClearance
// 0.05 m = 1.87 m (-> 17 micro; the old 16 = 1.78 m let the oracles accept a door the
// controller could not enter - Ravenmere G-77), auto step-up m_maxStepHeight = 4/9 m.
namespace Phyxel {
namespace Core {

constexpr int kAgentHalfWidthMicro = 2;
constexpr int kAgentHeightMicro    = 17;
constexpr int kAgentStepUpMicro    = 4;

}  // namespace Core
}  // namespace Phyxel
