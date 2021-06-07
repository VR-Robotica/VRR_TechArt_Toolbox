using UnityEngine;

namespace VRR.Avatar.Hands
{
    [System.Serializable]
    public struct RotationGoals
    {
        public Transform In;
        public Transform Out;
    }

    [System.Serializable]
    public struct MinMax
    {
        public float In;
        public float Out;
    }

    public enum RotationAxis { X, Y, Z }

    [System.Serializable]
    public abstract class Phalange
    {
        [SerializeField] protected string Name;
        [SerializeField] protected Transform[] Joints;
        [SerializeField] protected RotationAxis JointRotationAxis;
        [SerializeField] protected MinMax JointRotationLimit;
        [SerializeField] protected RotationGoals RotationGoals;
        [SerializeField] protected bool InvertRotation;
        [SerializeField] protected float SpreadAngle;
        [SerializeField] protected int BlendshapeCorrectionIndex;

        [SerializeField] protected SkinnedMeshRenderer SkinnedMesh;

        #region PRIVATE MEMBERS
        protected Quaternion[] _startRotations;
        protected Quaternion[] _adjustedStartRotations;

        protected Quaternion _targetRotation;
        protected Quaternion _spreadRotation;
        protected Vector3 _spreadAngles = new Vector3();
        protected Vector3 _jointRotation = new Vector3();
        protected Vector3 _jointRotationAxisMultiplier = new Vector3();

        protected float _threshold = 0.7f;
        protected float _adjustedAmount = 0.0f;

        protected float _prevBaseCurlAmount = 0.0f;
        protected float _prevTipCurlAmount = 0.0f;
        protected float _prevSpreadAmount = 0.0f;
        #endregion


        virtual public void Init()
        {
            GetStartRotations();
            SetRotationAxis(JointRotationAxis);
        }
        
        #region GETTERS
        public string GetName()
        {
            return Name;
        }

        public Transform[] GetJoints()
        {
            return Joints;
        }

        public MinMax GetJointRotationLimit()
        {
            return JointRotationLimit;
        }

        public float GetPreviousBaseCurlValue()
        {
            return _prevBaseCurlAmount;
        }

        public float GetPreviousTipCurlValue()
        {
            return _prevTipCurlAmount;
        }

        protected void GetStartRotations()
        {
            _startRotations = new Quaternion[Joints.Length];
            _adjustedStartRotations = new Quaternion[Joints.Length];

            for (int i = 0; i < _startRotations.Length; i++)
            {
                _startRotations[i].eulerAngles = Joints[i].localEulerAngles;
                _adjustedStartRotations[i].eulerAngles = Joints[i].localEulerAngles;
            }
        }

        public Transform GetJointsParent()
        {
            return Joints[0].transform.parent;
        }

        public Transform GetPhalangeRoot()
        {
            return Joints[0].transform;
        }

        #endregion

        #region SETTERS
        public void SetName(string incomingName)
        {
            Name = incomingName;
        }

        public void SetJoints(Transform[] joints)
        {
            Joints = joints;
        }

        public void SetSkinnedMesh(SkinnedMeshRenderer mesh)
        {
            SkinnedMesh = mesh;
        }

        public void SetThreshold(float amount)
        {
            _threshold = amount;
        }

        public void SetRotationAxis(RotationAxis axis)
        {
            switch (axis)
            {
                case RotationAxis.X:
                    _jointRotationAxisMultiplier = Vector3.right;
                    break;

                case RotationAxis.Y:
                    _jointRotationAxisMultiplier = Vector3.up;
                    break;

                case RotationAxis.Z:
                    _jointRotationAxisMultiplier = Vector3.forward;
                    break;

                default:
                    _jointRotationAxisMultiplier = Vector3.forward;
                    break;
            }
        }

        public void SetMinMaxRotation(MinMax amounts)
        {
            JointRotationLimit = amounts;
        }

        public void SetRotationGoals(RotationGoals goals)
        {
            RotationGoals = goals;
        }
        #endregion

        #region CORE FUNCTIONS
        public void Curl(float baseRotation, float tipRotation)
        {
            if (baseRotation != _prevBaseCurlAmount)
            {
                _prevBaseCurlAmount = baseRotation;
                RotateBaseJoints(baseRotation, tipRotation);
            }

            if (tipRotation != _prevTipCurlAmount)
            {
                _prevTipCurlAmount = tipRotation;
                RotateTipJoints(baseRotation, tipRotation);
            }
        }

        protected abstract void RotateBaseJoints(float amount, float tipRotation);

        protected abstract void RotateTipJoints(float amount, float tipRotation);
                
        protected void UpdateBaseStartRotation(Quaternion adjustedBaseRotationValue)
        {
            Quaternion baseJointStartRotation = adjustedBaseRotationValue;

            _adjustedStartRotations[0] = baseJointStartRotation;
        }

        protected void UpdateBlendshapeCorrections(float amount)
        {
            if (SkinnedMesh != null)
            {
                if (SkinnedMesh.sharedMesh.blendShapeCount > 0)
                {
                    SkinnedMesh.SetBlendShapeWeight(BlendshapeCorrectionIndex, amount * 100);
                }
            }
        }

        protected float RemapThreshold(float amount, float threshold)
        {
            float start1 = threshold;
            float stop1 = 1f;
            float start2 = 0f;
            float stop2 = 1f;

            return (start2 + (stop2 - start2) * ((amount - start1) / (stop1 - start1)));
        }
        #endregion
    }
}