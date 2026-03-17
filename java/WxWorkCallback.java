/**
 * LSPlant hook callback class — embedded as DEX bytes in the .so.
 *
 * Two callbacks:
 *   isPad         — replaces isAndroidPad*(), returns true
 *   onAppCreate   — replaces Application.onCreate(), calls original then
 *                   triggers the isAndroidPad* hook via native bridge
 */
public class WxWorkCallback {

    /** Replaces WeworkServiceImpl.isAndroidPad*(). Returns Boolean.TRUE. */
    public Object isPad(Object thiz, Object[] args) {
        return Boolean.TRUE;
    }

    /**
     * Replaces Application.onCreate().
     * Must call original first (via native bridge), then install WeWork hook.
     */
    public Object onAppCreate(Object thiz, Object[] args) {
        nativeCallOriginalOnCreate(thiz);
        nativeHookWework();
        return null;
    }

    /** Calls the original Application.onCreate() stored by LSPlant backup. */
    private native void nativeCallOriginalOnCreate(Object app);

    /** Triggers WeworkServiceImpl.isAndroidPad* hook from C++ side. */
    private native void nativeHookWework();
}
