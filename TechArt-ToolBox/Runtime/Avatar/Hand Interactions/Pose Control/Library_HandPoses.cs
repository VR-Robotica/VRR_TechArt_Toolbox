using System.Collections;
using System.Collections.Generic;
using UnityEngine;
using VRR.Avatar.Hands;

namespace VRR.Libraries
{
    [CreateAssetMenu(menuName = "VRR/Library/Hand Poses")]
    public class Library_HandPoses : Library_Asset
    {
        #region EDITOR SET VARIABLES
        #pragma warning disable 0649
        [SerializeField] private HandPose_Asset[] Library;
        #pragma warning restore 0649
        #endregion

        int userID = 0;
        int index = 0;

        public HandPose_Asset GetPoseAsset(int index)
        {
            return Library[index];
        }
        public HandPose_Asset GetPoseAsset(HandPose_Types pose)
        {
            for (int i = 0; i < Library.Length; i++)
            {
                if (pose == Library[i].GetHandPoseType())
                {
                    return Library[i];
                }
            }

            Debug.LogWarning(pose.ToString() + " not found in library.");

            return null;
        }
    }
}