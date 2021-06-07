using UnityEngine;

namespace VRR.TechArtTools.Utilities
{
    public abstract class TextureSwapper : MonoBehaviour
    {
        [SerializeField] protected Texture2D[] _set1;
        [SerializeField] protected Texture2D[] _set2;

        public virtual void SwapDaylight()
        {
            Swap(_set1);
        }

        public virtual void SwapNightlight()
        {
            Swap(_set2);
        }

        protected abstract void Swap(Texture2D[] textureSet);
    }
}