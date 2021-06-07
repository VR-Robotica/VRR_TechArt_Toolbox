using System.Collections;
using System.Collections.Generic;
using UnityEngine;

namespace VRR.Avatar.Hands
{
    [CreateAssetMenu(menuName = "VRR/Hand Pose Asset")]
    public class HandPose_Asset : ScriptableObject
    {
        #region EDITOR SET VARIABLES
#pragma warning disable 0168
        [SerializeField] private HandPose_Types PoseType = HandPose_Types.Free;
        [Space]
        [SerializeField] private float[] FingerBaseCurlValues;
        [SerializeField] private float[] FingerTipCurlValues;
        [Space]
        [SerializeField] private float[] ThumbBaseCurlValues;
        [SerializeField] private float[] ThumbTipCurlValues;
        #pragma warning restore 0168
        #endregion


        #region GETTERS
        public string GetName()
        {
            return PoseType.ToString();
        }

        public HandPose_Types GetHandPoseType()
        {
            return PoseType;
        }

        public float[] GetFingerBaseCurlValues()
        {
            return FingerBaseCurlValues;
        }
        public float[] GetFingerTipCurlValues()
        {
            return FingerTipCurlValues;
        }
        public float[] GetThumbBaseCurlValues()
        {
            return ThumbBaseCurlValues;
        }
        public float[] GetThumbTipCurlValues()
        {
            return ThumbTipCurlValues;
        }


        public float GetFingerBaseCurlValue(int index)
        {
            return FingerBaseCurlValues[index];
        }
        public float GetFingerTipCurlValue(int index)
        {
            return FingerTipCurlValues[index];
        }
        public float GetThumbBaseCurlValue(int index)
        {
            return ThumbBaseCurlValues[index];
        }
        public float GetThumbTipCurlValue(int index)
        {
            return ThumbTipCurlValues[index];
        }
        #endregion

        #region SETTERS
        public void SetFingerBaseCurlValues(float[] array)
        {
            FingerBaseCurlValues = array;
        }
        public void SetFingerTipCurlValues(float[] array)
        {
            FingerTipCurlValues = array;
        }
        public void SetThumbBaseCurlValues(float[] array)
        {
            ThumbBaseCurlValues = array;
        }
        public void SetThumbTipCurlValues(float[] array)
        {
            ThumbTipCurlValues = array;
        }
        #endregion
    }
}