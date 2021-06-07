using UnityEngine;

namespace VRR.Avatar.Blendshape
{
    public abstract class BlendShape_Controller : MonoBehaviour
    {
        [SerializeField] protected SkinnedMeshRenderer skinnedMesh;

        protected Vector2Int _axisValues;

        /// <summary>
        /// Use 0-100 integers
        /// </summary>
        /// <param name="xPos"></param>
        /// <param name="yPos"></param>
        public abstract void SetBlendShapeValues(int xPos, int yPos);

        protected abstract void ResetShapes();
          
        public Vector2Int GetAxisValues()
        {
            return _axisValues;
        }
    }
}