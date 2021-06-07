using System.Collections;
using UnityEngine;

namespace VRR.Avatar.Hands
{
    public enum UpdateFrameRate { EveryFrame, FPS_24, FPS_30}

    public class HandPose_Controller : MonoBehaviour
    {
        #pragma warning disable 0649
        [SerializeField] private UpdateFrameRate FrameRateUpdate = UpdateFrameRate.EveryFrame;
        [SerializeField] private SkinnedMeshRenderer HandMesh;
        [SerializeField] private bool InvertRay;
        #pragma warning restore 0649

        private WaitForSeconds _waitTime;
        public Phalanges_Hand Hand;

        [HideInInspector] public float[] FingerBaseCurlAmounts { get; private set; }
        [HideInInspector] public float[] FingerTipCurlAmounts { get; private set; }
        [HideInInspector] public float[] ThumbBaseCurlAmounts { get; private set; }
        [HideInInspector] public float[] ThumbTipCurlAmounts { get; private set; }

        [HideInInspector] public float   FingerSpreadAmount { get; private set; }

        #region MONOBEHAVIORS
        private void Awake()
        {
            FingerBaseCurlAmounts = new float[Hand.GetFingerLength()];
            FingerTipCurlAmounts = new float[Hand.GetFingerLength()];
            ThumbBaseCurlAmounts = new float[Hand.GetThumbLength()];
            ThumbTipCurlAmounts = new float[Hand.GetThumbLength()];

            Hand.Init();
            SetSkinnedMesh();
            InitWaitTimes();
        }
        
        private void OnEnable()
        {
            StartUpdatingFingerValues();
        }

        private void OnDisable()
        {
            StopUpdatingFingerValues();
        }
        #endregion

        #region SETTERS
        public void SetSkinnedMesh(SkinnedMeshRenderer mesh)
        {
            HandMesh = mesh;
        }

        private void SetSkinnedMesh()
        {
            for (int i = 0; i < Hand.GetFingerLength(); i++)
            {
                Hand.SetFingerMeshReference(i, HandMesh);
            }

            for (int i = 0; i < Hand.GetThumbLength(); i++)
            {
                Hand.SetThumbMeshReference(i, HandMesh);
            }
        }
        #endregion

        #region VALUE UPDATE COROUTINE
        private void InitWaitTimes()
        {
            switch (FrameRateUpdate)
            {
                case UpdateFrameRate.FPS_24:
                    _waitTime = new WaitForSeconds(1 / 24);
                    break;

                case UpdateFrameRate.FPS_30:
                    _waitTime = new WaitForSeconds(1 / 30);
                    break;
            }
        }

        private void StartUpdatingFingerValues()
        {
            StopUpdatingFingerValues();
            UpdateFingerValues_coroutine = StartCoroutine(UpdateFingerValues());
        }

        private void StopUpdatingFingerValues()
        {
            if(UpdateFingerValues_coroutine != null)
            {
                StopCoroutine(UpdateFingerValues_coroutine);
                UpdateFingerValues_coroutine = null;
            }
        }

        private Coroutine UpdateFingerValues_coroutine;
        private IEnumerator UpdateFingerValues()
        {
            while (true)
            {
                // Loop through fingers and adjust value
                for (int i = 0; i < FingerBaseCurlAmounts.Length; i++)
                {
                    Hand.SetFingerCurl(i, FingerBaseCurlAmounts[i], FingerTipCurlAmounts[i]);
                }

                // Loop through thumbs and adjust value
                for (int i = 0; i < ThumbBaseCurlAmounts.Length; i++)
                {
                    Hand.SetThumbCurl(i, ThumbBaseCurlAmounts[i], ThumbTipCurlAmounts[i]);
                }

                switch (FrameRateUpdate)
                {
                    case UpdateFrameRate.EveryFrame:
                        yield return new WaitForEndOfFrame();
                        break;

                    default:
                        yield return _waitTime;
                        break;
                }
            }
        }
        #endregion

        #region DEBUG VISUALS
        #if UNITY_EDITOR

        public bool ShowDebugLog;
        public bool ShowGizmos;

        [Range(0.001f, 0.01f)]
        public float GizmoSize = 0.01f;
        [Range(0.01f, 0.1f)]
        public float GizmoRaySize = 0.05f;

        private void drawJointGizmos(Transform[] jointChain, float rotationLimit)
        {
            Vector3 cubeSize = new Vector3(GizmoSize, GizmoSize, GizmoSize);

            for (int i = 0; i < jointChain.Length; i++)
            {
                try
                {
                    // add gizmos to each joint, use different icon for End joints
                    if (i != jointChain.Length - 1)
                    {
                        Gizmos.color = Color.blue;
                        Gizmos.DrawCube(jointChain[i].position, cubeSize);
                        Gizmos.DrawLine(jointChain[i].position, jointChain[i + 1].position);

                        UnityEditor.Handles.color = Color.blue;
                        if (i == 0)
                        {
                            UnityEditor.Handles.DrawWireDisc(jointChain[i].position, jointChain[i].TransformDirection(Vector3.forward), GizmoSize * 2.0f);
                        }
                        else
                        {
                            UnityEditor.Handles.DrawWireDisc(jointChain[i].position, jointChain[i].TransformDirection(Vector3.forward), GizmoSize);
                        }
                    }
                    else
                    {
                        Gizmos.color = Color.red;
                        Gizmos.DrawSphere(jointChain[i].position, GizmoSize);

                        Gizmos.color = Color.green;
                        Vector3 direction;

                        if(!InvertRay)
                        {
                            direction = jointChain[i].TransformDirection(Vector3.up) * GizmoRaySize;
                        }
                        else
                        {
                            direction = jointChain[i].TransformDirection(Vector3.down) * GizmoRaySize;
                        }

                        Gizmos.DrawRay(jointChain[i].position, direction);
                    }
                }
                catch (System.Exception)
                {
                    if (ShowDebugLog) { Debug.Log("Some Elements of the Joint Chain are Undefined."); }
                }
            }
        }

        private void OnDrawGizmos()
        {
            if (ShowGizmos)
            {
                for (int i = 0; i < Hand.GetFingerLength(); i++)
                {
                    drawJointGizmos(Hand.GetFingerJointTransform(i), Hand.GetFingerRotationLimitIn(i));
                }

                for (int i = 0; i < Hand.GetThumbLength(); i++)
                {
                    drawJointGizmos(Hand.GetThumbJointTransforms(i), Hand.GetThumbRotationLimitIn(i));
                }
            }
        }
        #endif
        #endregion
    }
}