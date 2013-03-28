	import java.math.BigInteger;

//--------------------------------------------------------------------------
// Configuration
//--------------------------------------------------------------------------

	String localControllerEndpointURL	= "http://" + InetAddress.getLocalHost().getCanonicalHostName() + ":8089/controller";
	String sessionManagementEndpointURL	= System.getProperty("testbed.sm.endpointurl");
	String secretReservationKeys        = System.getProperty("testbed.secretreservationkeys");
	//String messageToSend                = System.getProperty("testbed.message");
	String selectedNodeUrns               = System.getProperty("testbed.nodeurns");
	boolean csv                         = System.getProperty("testbed.listtype") != null && "csv".equals(System.getProperty("testbed.listtype"));

	String protobufHost                 = System.getProperty("testbed.protobuf.hostname");
	String protobufPortString           = System.getProperty("testbed.protobuf.port");
	Integer protobufPort                = protobufPortString == null ? null : Integer.parseInt(protobufPortString);
	boolean useProtobuf                 = protobufHost != null && protobufPort != null;

 	SessionManagement sessionManagement = WSNServiceHelper.getSessionManagementService(sessionManagementEndpointURL);

 //--------------------------------------------------------------------------
// Application logic
//--------------------------------------------------------------------------
	
	
	
	log.debug("Using the following parameters for calling getInstance(): {}, {}",
			StringUtils.jaxbMarshal(helper.parseSecretReservationKeys(secretReservationKeys)),
			localControllerEndpointURL
	);

	Controller controller = new Controller() {
		public void receive(List msg) {
			// nothing to do
		}
		public void receiveStatus(List requestStatuses) {
			wsn.receive(requestStatuses);
		}
		public void receiveNotification(List msgs) {
			for (int i=0; i<msgs.size(); i++) {
				log.info(msgs.get(i));
			}
		}
		public void experimentEnded() {
			log.debug("Experiment ended");
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
	
	byte SLIP_END = 192;
	
	File  f_pipe = new File ( "/tmp/wisebed_sending_pipe" );  
	for(;;){
		RandomAccessFile raf = new RandomAccessFile(f_pipe, "r");//p.1
// 		System.out.println( "open" );
		byte[] pipe_buffer = new byte[1500];
		int actual_pos = 0;
		
		for( ;; ){
			
			int tmp;
			try
			{
				tmp = raf.readUnsignedByte();
			}catch( IOException e ) {
				break;
			}
// 			System.out.println( "read" );
// 				System.out.println( tmp );
				pipe_buffer[actual_pos] = (byte)tmp;
				
				if( pipe_buffer[actual_pos] == SLIP_END && actual_pos > 1 ) {
					
// 					System.out.println( "send " + actual_pos );
					
					//copy the real size
					byte[] messageToSendBytes = new byte[actual_pos];
					for( int i = 0; i < actual_pos; i++ )
						messageToSendBytes[i] = pipe_buffer[i];
				
					// Constructing the Message
					Message binaryMessage = new Message();
					binaryMessage.setBinaryData(messageToSendBytes);
					
					GregorianCalendar c = new GregorianCalendar();
					c.setTimeInMillis(System.currentTimeMillis());

					binaryMessage.setTimestamp(DatatypeFactory.newInstance().newXMLGregorianCalendar(c));
					binaryMessage.setSourceNodeId("urn:wisebed:uzl1:0xFFFF");
					
					Future sendFuture = wsn.send(nodeURNs, binaryMessage, 10, TimeUnit.SECONDS);
					try {

						JobResult sendJobResult = sendFuture.get();
						sendJobResult.printResults(System.out, csv);
// 						System.exit(sendJobResult.getSuccessPercent() < 100 ? 1 : 0);

// 						log.debug("Shutting down...");
// 						System.exit(0);

					} catch (ExecutionException e) {
						if (e.getCause() instanceof TimeoutException) {
							log.info("Call timed out. Exiting...");
						}
						System.exit(1);
					}
	
					actual_pos = 0;
				}
				else{
					actual_pos++;
				}

			
		}
	}
	
	raf.close();
