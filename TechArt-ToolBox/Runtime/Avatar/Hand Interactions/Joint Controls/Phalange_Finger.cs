using UnityEngine;

namespace VRR.Avatar.Hands
{
    [System.Serializable]
    public class Phalange_Finger : Phalange
    {
        private float baseInfluence = 0.4f; // these values must equal 1
        private float tipInfluence = 0.6f;  // ie basInfluence + tipInfluence = 1.0f

        protected override void RotateBaseJoints(float amount, float tipRotation)
        {
            Transform baseJoint         = Joints[0];
            Quaternion startRotation    = _startRotations[0];
            Quaternion targetIN         = RotationGoals.In.localRotation;
            Quaternion targetOUT        = RotationGoals.Out.localRotation;
            Quaternion targetRotation   = Quaternion.identity;

            if(targetIN == null)
            {
                targetIN = Quaternion.Euler(0, 0, 90);
            }

            if(targetOUT == null)
            {
                targetOUT = Quaternion.Euler(0, 0, -10);
            }

            float blendshapeTargetValue = 0f; 

            if (amount < 0)
            {
                targetRotation          = targetOUT;
                blendshapeTargetValue   = 0;
            }
            else
            if(amount >= 0 && amount <= _threshold)
            {
                targetRotation          = startRotation; // RotationGoals.In.localRotation;
                blendshapeTargetValue   = (amount * baseInfluence) + (tipRotation * tipInfluence); // value between 0-1
            }
            else
            if (amount > _threshold)
            {
                amount                  = RemapThreshold(amount, _threshold);

                startRotation           = _adjustedStartRotations[0];
                targetRotation          = targetIN;
                blendshapeTargetValue   = (amount * baseInfluence) + (tipRotation * tipInfluence); // value between 0-1
            }

            baseJoint.localRotation = Quaternion.Slerp(startRotation, targetRotation, Mathf.Abs(amount));
            UpdateBlendshapeCorrections(blendshapeTargetValue);
        }

        protected override void RotateTipJoints(float amount, float tipRotation)
        {
            int invertValue = 1;
            if (InvertRotation) invertValue = -1;

            for (int i = 1; i < Joints.Length; i++)
            {
                if (amount < 0)
                {
                    _jointRotation.z = JointRotationLimit.Out * invertValue;
                    _targetRotation.eulerAngles = _jointRotation;
                }
                else
                {
                    if (i > 1)
                    {
                        _jointRotation.z = JointRotationLimit.In * tipRotation * invertValue;
                    }
                    else
                    {
                        _jointRotation.z = JointRotationLimit.In * invertValue;
                    }

                    _targetRotation.eulerAngles = (-1 * _jointRotation);
                }

                Joints[i].localRotation = Quaternion.Slerp(_startRotations[i], _targetRotation, Mathf.Abs(amount));
            }
        }
    }
}