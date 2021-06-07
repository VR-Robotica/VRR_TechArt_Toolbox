using UnityEngine;

namespace VRR.Avatar.Hands
{
    [System.Serializable]
    public class Phalanges_Hand
    {
        #region EDITOR SET VARIABLES
        #pragma warning disable 0649
        // Custom setting for the finger tip curling action
        [SerializeField] private float CurlThreshold = 0.5f;         // Base joint remains stationary until 50% of the grip trigger has been pulled

        // Custom setting for the fingers
        // some hands may have 5 fingers, some only 2.
        [SerializeField] public Phalange_Finger[] Fingers;

        // Custom setting for thumbs
        // some hands may have 1 thumb, some maybe 2.
        [SerializeField] public Phalange_Thumb[] Thumbs;
        
        #pragma warning restore 0649
        #endregion

        public void Init()
        {
            for (int i = 0; i < Fingers.Length; i++)
            {
                Fingers[i].Init();
                Fingers[i].SetThreshold(CurlThreshold);
            }

            for (int i = 0; i < Thumbs.Length; i++)
            {
                Thumbs[i].Init();
            }
        }

        #region GETTERS
        public int GetFingerLength()
        {
            if (Fingers != null)
            {
                return Fingers.Length;
            }

            return 0;
        }

        public int GetThumbLength()
        {
            if (Thumbs != null)
            {
                return Thumbs.Length;
            }

            return 0;
        }

        public Transform[] GetFingerJointTransform(int index)
        {
            return Fingers[index].GetJoints();
        }

        public Transform[] GetThumbJointTransforms(int index)
        {
            return Thumbs[index].GetJoints();
        }

        public float GetFingerRotationLimitIn(int index)
        {
            return Fingers[index].GetJointRotationLimit().In;
        }

        public float GetThumbRotationLimitIn(int index)
        {
            return Thumbs[index].GetJointRotationLimit().In;
        }

        public string GetFingerName(int index)
        {
            return Fingers[index].GetName();
        }

        public string GetThumbName(int index)
        {
            return Thumbs[index].GetName();
        }
        #endregion

        #region SETTERS
        public void SetFingerCurl(int index, float baseAmount, float tipAmount)
        {
            Fingers[index].Curl(baseAmount, tipAmount);
        }

        public void SetThumbCurl(int index, float baseAmount, float tipAmount)
        {
            Thumbs[index].Curl(baseAmount, tipAmount);
        }

        public void SetFingerMeshReference(int index, SkinnedMeshRenderer mesh)
        {
            Fingers[index].SetSkinnedMesh(mesh);
        }

        public void SetThumbMeshReference(int index, SkinnedMeshRenderer mesh)
        {
            Thumbs[index].SetSkinnedMesh(mesh);
        }

        public void SetFingerArray(Phalange_Finger[] fingers)
        {
            Fingers = fingers;
        }

        public void SetThumbArray(Phalange_Thumb[] thumbs)
        {
            Thumbs = thumbs;
        }

        #endregion
    }
}