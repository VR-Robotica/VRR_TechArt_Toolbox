using System.Collections;
using System.Collections.Generic;
using UnityEngine;

namespace VRR.Avatar.Emote
{
    public class FaceEmote_Controller : MonoBehaviour
    {
        private SkinnedMeshRenderer _skinnedMeshRenderer;

        public void SetSkinnedMesh(SkinnedMeshRenderer renderer)
        {
            _skinnedMeshRenderer = renderer;
        }

        public void SetBlendShapeValues(int index, int amount)
        {
            if (_skinnedMeshRenderer != null)
            {
                _skinnedMeshRenderer.SetBlendShapeWeight(index, amount);
            }
            else
            {
                Debug.LogWarning("FaceEmote_Controller - Skinned Mesh not yet set!");
            }
        }

        public void ResetBlendshapes()
        {
            if(_skinnedMeshRenderer != null)
            {
                for(int i = 0; i < _skinnedMeshRenderer.sharedMesh.blendShapeCount; i++)
                {
                    _skinnedMeshRenderer.SetBlendShapeWeight(i, 0);
                }
            }
        }
    }
}