//--------------------------------------------------------------------------
// Configuration
//--------------------------------------------------------------------------

	String localControllerEndpointURL	= "http://" + InetAddress.getLocalHost().getCanonicalHostName() + ":8091/controller";
	String secretReservationKeys        = System.getProperty("testbed.secretreservationkeys");
	String selectedNodeUrns               = System.getProperty("testbed.nodeurns");
	String sessionManagementEndpointURL	= System.getProperty("testbed.sm.endpointurl");
	boolean csv                         = System.getProperty("testbed.listtype") != null && "csv".equals(System.getProperty("testbed.listtype"));

	String protobufHost                 = System.getProperty("testbed.protobuf.hostname");
	String protobufPortString           = System.getProperty("testbed.protobuf.port");
	Integer protobufPort                = protobufPortString == null ? null : Integer.parseInt(protobufPortString);
	boolean useProtobuf                 = protobufHost != null && protobufPort != null;

	SessionManagement sessionManagement = WSNServiceHelper.getSessionManagementService(sessionManagementEndpointURL);

//--------------------------------------------------------------------------
// Application logic
//--------------------------------------------------------------------------

	Controller controller = new Controller() {
		public void receive(List msgs) {
			File  f_pipe = new File ( "/tmp/wisebed_listening_pipe" );  
			
			for (int i=0; i<msgs.size(); i++) {
			
				RandomAccessFile raf = new RandomAccessFile(f_pipe, "rw");//p.1
				Message msg = (Message) msgs.get(i);
				synchronized(System.out) {
					
// 					String text = StringUtils.replaceNonPrintableAsciiCharacters(new String(msg.getBinaryData()));
// 
// 					if (csv) {
// 						text = text.replaceAll(";", "\\;");
// 					}
					//System.out.print(new org.joda.time.DateTime(msg.getTimestamp().toGregorianCalendar()));
					//System.out.print(csv ? ";" : " | ");
					
					//Display only from the given node
					if( msg.getSourceNodeId().equals( nodeURNs.get(0) ) )
					{
						byte[] byte_msg = msg.getBinaryData();
						
// 						System.err.print(msg.getSourceNodeId()+"|"+msg.getBinaryData().length+"|"+byte_msg+"\n");
						
						//The UART messages start with an i character
						if( byte_msg[0] == 'i' )
						{
							//Drop the initial 'i' here
// 							for( int k = 1; k < byte_msg.length; k++ )
								raf.write( byte_msg, 1, byte_msg.length-1 );
// 								System.out.write( byte_msg[k]);
						}
// 						System.out.flush();
					}
					//System.out.print(csv ? ";" : " | ");
					//System.out.print(text);
// 					System.out.print();
// 					System.out.print();
					//System.out.println();
            	}
			}
		}
		public void receiveStatus(List requestStatuses) {
			// nothing to do
		}
		public void receiveNotification(List msgs) {
			for (int i=0; i<msgs.size(); i++) {
				System.err.print(new org.joda.time.DateTime());
				System.err.print(csv ? ";" : " | ");
				System.err.print("Notification");
				System.err.print(csv ? ";" : " | ");
				System.err.print(msgs.get(i));
				System.err.println();
			}
		}
		public void experimentEnded() {
			System.err.println("Experiment ended");
			System.exit(0);
		}
	};

	// try to connect via unofficial protocol buffers API if hostname and port are set in the configuration
    if (useProtobuf) {

		ProtobufControllerClient pcc = ProtobufControllerClient.create(
				protobufHost,
				protobufPort,
				helper.parseSecretReservationKeys(secretReservationKeys)
		);
		pcc.addListener(new ProtobufControllerAdapter(controller));
		try {
			pcc.connect();
		} catch (Exception e) {
			useProtobuf = false;
		}
	}

	if (!useProtobuf) {

		DelegatingController delegator = new DelegatingController(controller);
		delegator.publish(localControllerEndpointURL);
		log.debug("Local controller published on url: {}", localControllerEndpointURL);

	}

	log.debug("Using the following parameters for calling getInstance(): {}, {}",
			StringUtils.jaxbMarshal(helper.parseSecretReservationKeys(secretReservationKeys)),
			localControllerEndpointURL
	);

	String wsnEndpointURL = null;
	try {
		wsnEndpointURL = sessionManagement.getInstance(
				helper.parseSecretReservationKeys(secretReservationKeys),
				(useProtobuf ? "NONE" : localControllerEndpointURL)
		);
	} catch (UnknownReservationIdException_Exception e) {
		log.warn("There was no reservation found with the given secret reservation key. Exiting.");
		System.exit(1);
	}

	log.debug("Got a WSN instance URL, endpoint is: {}", wsnEndpointURL);
	WSN wsnService = WSNServiceHelper.getWSNService(wsnEndpointURL);
	final WSNAsyncWrapper wsn = WSNAsyncWrapper.of(wsnService);
	
	// retrieve reserved node URNs from testbed
	List nodeURNs;
	if (selectedNodeUrns != null && !"".equals(selectedNodeUrns)) {
		nodeURNs = Lists.newArrayList(selectedNodeUrns.split(","));
		log.debug("Selected the following node URNs: {}", nodeURNs);
	} else {
		nodeURNs = WiseMLHelper.getNodeUrns(wsn.getNetwork().get(), new String[]{});
		log.debug("Retrieved the following node URNs: {}", nodeURNs);
	}
	
	while(true) {
// 		System.err.println( "READ WHILE ");
		Thread.sleep(60*1000);
// 		try {
// 			System.in.read();
// 		} catch (Exception e) {
// 			System.err.println(e);
// 		}
	}
