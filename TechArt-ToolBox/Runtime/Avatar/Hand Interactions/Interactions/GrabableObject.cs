using UnityEngine;
using VRR.Avatar.Hands;
using VRR.Input;

namespace VRR.Interactions
{
    [RequireComponent(typeof(Rigidbody))]
    [RequireComponent(typeof(Collider))]
    public class GrabableObject : MonoBehaviour, IGrabbable
    {
        #region EDITOR SET VARIABLES
        #pragma warning disable 0649

        [SerializeField] private HandPose_Types _poseType;
        [SerializeField] private Vector3        _positionOffset;
        [SerializeField] private Vector3        _rotationOffset;

        #pragma warning restore 0649
        #endregion

        private Rigidbody _rigidbody;
        private Transform _originalParent;

        #region MONOBEHAVIORS
        private void Awake()
        {
            SetParentReference(this.transform.parent);
            _rigidbody = this.gameObject.GetComponent<Rigidbody>();
        }
        #endregion

        #region GETTERS
        public HandPose_Types GetPose()
        {
            return _poseType;
        }

        public Vector3 GetPositionOffset(ControllerHand handedness)
        {
            switch(handedness)
            {
                case ControllerHand.Left:
                return Vector3.Reflect(_positionOffset, Vector3.right);
            }

            return _positionOffset;
        }

        public Vector3 GetRotationOffset(ControllerHand handedness)
        {
            switch (handedness)
            {
                case ControllerHand.Left:
                return Vector3.Reflect(_rotationOffset, Vector3.up);
            }

            return _rotationOffset;
        }
        #endregion

        #region SETTERS
        private void SetParentReference(Transform originalParent)
        {
            _originalParent = originalParent;
        }
        #endregion

        #region ACTIONS
        public void Grabbed()
        {
            DisableRigidbody();
        }
        public void Released(Vector3 velocity, Vector3 angularVelocity, float force)
        {
            ReturnToParent();
            EnableRigidBody();

            _rigidbody.velocity = velocity;
            _rigidbody.angularVelocity = angularVelocity;

            _rigidbody.AddForceAtPosition(velocity * force, this.transform.position, ForceMode.Impulse);
        }
        #endregion

        #region RIGIDBODY CONTROL
        private void ReturnToParent()
        {
            this.transform.SetParent(_originalParent);
        }
        private void EnableRigidBody()
        {
            RigidBodySetActive(true);
        }
        private void DisableRigidbody()
        {
            RigidBodySetActive(false);
        }
        private void RigidBodySetActive(bool value)
        {
            if (_rigidbody == null)
            {
                _rigidbody = this.gameObject.GetComponent<Rigidbody>();
            }

            _rigidbody.isKinematic = !value;
            //_rigidbody.detectCollisions = value;
        }        
        #endregion

        #region CUSTOM EDITOR CALLBACK
        public void Save()
        {
            _positionOffset = this.transform.localPosition;
            _rotationOffset = this.transform.localRotation.eulerAngles;

            if (_rigidbody == null)
            {
                _rigidbody = this.gameObject.GetComponent<Rigidbody>();
            }
        }

        #endregion
    }
}