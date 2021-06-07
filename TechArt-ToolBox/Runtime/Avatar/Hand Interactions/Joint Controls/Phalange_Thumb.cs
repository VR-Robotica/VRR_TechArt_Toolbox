using UnityEngine;

namespace VRR.Avatar.Hands
{
    [System.Serializable]
    public class Phalange_Thumb : Phalange
    {
        private float baseInfluence = 0.5f; // these values must equal 1
        private float tipInfluence = 0.5f;  // ie basInfluence + tipInfluence = 1.0f

        protected override void RotateBaseJoints(float amount, float tipRotation)
        {
            Transform baseJoint = Joints[0];
            Quaternion startRotation = _startRotations[0];
            Quaternion targetRotation = RotationGoals.In.localRotation;

            if (amount > 0)
            {
                targetRotation = RotationGoals.In.localRotation;
                UpdateBlendshapeCorrections((amount * baseInfluence) + (tipRotation * tipInfluence));
            }
            else
            {
                targetRotation = startRotation;
                UpdateBlendshapeCorrections(0);
            }

            baseJoint.localRotation = Quaternion.Slerp(startRotation, targetRotation, Mathf.Abs(amount));
        }

        protected override void RotateTipJoints(float amount, float tipRotation)
        {
            int invertValue = 1;
            if (InvertRotation) invertValue = -1;

            for (int i = 1; i < Joints.Length; i++)
            {
                if (tipRotation < 0)
                {
                    if (i == 1)
                    {
                        _targetRotation = RotationGoals.Out.localRotation;
                    }
                    else
                    {
                        _jointRotation.z = JointRotationLimit.Out * tipRotation * invertValue;
                        _targetRotation.eulerAngles = _jointRotation;
                    }
                }
                else
                {
                    _jointRotation.z = JointRotationLimit.In * tipRotation * invertValue;
                    _targetRotation.eulerAngles = (-1 * _jointRotation);
                }

                Joints[i].localRotation = Quaternion.Slerp(_startRotations[i], _targetRotation, Mathf.Abs(tipRotation));
            }
        }
    }
}