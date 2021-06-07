using System.Collections;
using System.Collections.Generic;
using UnityEngine;
using VRR.Input;
using VRR.Interactions;
using VRR.TechArtTools.Utilities;

namespace VRR.Avatar.Hands
{
    [RequireComponent(typeof(SphereCollider))]
    public class HandInteraction : MonoBehaviour
    {
        [SerializeField] private ControllerHand    _handedness;
        [SerializeField] private Vector3 _currentVelocity;
        [SerializeField] private Vector3 _currentAngularVelocity;
        [SerializeField] public List<GrabableObject> _objectsWithinReach = new List<GrabableObject>();

        private bool           _isGrabbing;
        private GrabableObject _objectInHand;
        private HandPose_Types _poseType = HandPose_Types.Free;

        private Vector3        _previousPosition;
        private Quaternion     _previousRotation;


        #region MONOBEHAVIOR CALLBACKS
        private void OnEnable()
        {
            StartTracking();
        }
        private void OnDisable()
        {
            StopTracking();
        }
        private void Update()
        {
            // check to see if object was grabbed by another hand
            // and is no longer parented to this hand

            // a. Controller is grabbing (beyond the 0.9 theshold)
            // b. Nothing is already in our hand
            // c. And there are valid objects within reach
            if (_isGrabbing && !_objectInHand && _objectsWithinReach.Count > 0)
            {
                //Step 1: get the nearest object reference
                Grab(GetNearestObject(_objectsWithinReach));
            }

            // a. we have an object in our hand but the controller is no longer grabbing (ie less than the 0.9 theshold)
            // OR
            // b. we have a ref to an object in our hand but it now has a different parent
            if ( (_objectInHand && !_isGrabbing) || (_objectInHand && _objectInHand.transform.parent != this.transform) )
            {
                // Step 1: release the object that is in our hand
                Release(_objectInHand);
            }
        }        
        private void OnTriggerEnter(Collider collider)
        {
            // Adding objects to list of potential objects to grab
            GrabableObject enteringObject = collider.GetComponent<GrabableObject>();

            if ( !IsGrabbingSomething() && !_objectsWithinReach.Contains(enteringObject))
            {
                _objectsWithinReach.Add(enteringObject);
            }
        }
        private void OnTriggerExit(Collider collider)
        {
            // removing objects from list of potentials
            GrabableObject leavingObject = collider.GetComponent<GrabableObject>();

            if (_objectsWithinReach.Contains(leavingObject))
            {
                _objectsWithinReach.Remove(leavingObject);
            }
        }
        #endregion

        #region GETTERS
        private GrabableObject GetNearestObject(List<GrabableObject> objects)
        {
            float nearestDistance        = 10.0f;
            GrabableObject nearestObject = null;

            // Step 1: check the distance between each object and this hand
            foreach (GrabableObject obj in objects)
            {
                float distance = Vector3.Distance(this.transform.position, obj.transform.position);

                // Step 2: If the next distance is less than the previous distance, 
                // cache the distance and the object
                if (distance < nearestDistance)
                {
                    nearestDistance = distance;
                    nearestObject = obj;
                }
            }

            // Step 3: return the final nearest object
            return nearestObject;
        }
        public HandPose_Types GetPose()
        {
            _poseType = _objectInHand.GetPose();

            return _poseType;
        }
        public bool IsGrabbingSomething()
        {
            return _isGrabbing && _objectInHand != null;
        }
        public GameObject GetGrabbedObject()
        {
            if (_objectInHand != null)
            {
                return _objectInHand.gameObject;
            }

            return null;
        }
        #endregion

        #region SETTERS
        /// <summary>
        /// Hand Pose Manager passes along the boolean value from the 
        /// HandInput Manager to say whether or not we are grabbing
        /// 
        /// NOTE: Should probably use an Event for this
        /// 
        /// </summary>
        /// <param name="value"></param>
        public void SetGrabbingValue(bool value)
        {
            _isGrabbing = value;
        }

        public void SetHandedness(ControllerHand handedness)
        {
            _handedness = handedness;
        }
        #endregion

        #region ACTIONS
        private void Grab(GrabableObject grabbedObject)
        {
            // Step 1: cache the grabbed Object as our object in hand
            _objectInHand = grabbedObject;

            // Step 2: Parent the grabbed object to our hand
            grabbedObject.transform.SetParent(this.transform);

            // Step 3: adjust position of object to fit with hand pose
            grabbedObject.transform.localPosition = grabbedObject.GetPositionOffset(_handedness);
            grabbedObject.transform.localRotation = Quaternion.Euler(grabbedObject.GetRotationOffset(_handedness));

            // Step 4: Set our hand pose
            _poseType = grabbedObject.GetPose();

            // Step 5: stop rigidbody calculations
            grabbedObject.Grabbed();
        }

        private void Release(GrabableObject objectInHand)
        {
            _objectInHand = null;

            float force = _currentVelocity.magnitude;
            float multiplier = 1.25f;
            
            objectInHand.Released(_currentVelocity, _currentAngularVelocity, force * multiplier);
        }

        #endregion

        #region TRACKING VELOCITY COROUTINE

        private void StartTracking()
        {
            StopTracking();

            trackingCoroutine = StartCoroutine(TrackVelocities());
        }
        private void StopTracking()
        {
            if (trackingCoroutine != null)
            {
                StopCoroutine(trackingCoroutine);
                trackingCoroutine = null;
            }
        }

        private Coroutine trackingCoroutine;
        private WaitForEndOfFrame endOfFrame = new WaitForEndOfFrame();
        private IEnumerator TrackVelocities()
        {
            while (true)
            {
                _previousPosition = this.transform.position;
                _previousRotation = this.transform.rotation;

                yield return endOfFrame;

                _currentVelocity = CustomUtilities.CalculateVelocity(this.transform.position, _previousPosition);
                _currentAngularVelocity = CustomUtilities.CalculateAngularVelocity(this.transform.rotation, _previousRotation);
            }
        }
        #endregion
    }
}